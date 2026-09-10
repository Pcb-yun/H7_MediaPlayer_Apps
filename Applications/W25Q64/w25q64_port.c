#include "driver_w25qxx_interface.h"
#include "w25q64_port.h"

static w25qxx_handle_t gs_handle;

bool W25Q64_Init(void) {
    uint8_t res;

    DRIVER_W25QXX_LINK_INIT(&gs_handle, w25qxx_handle_t);
    DRIVER_W25QXX_LINK_SPI_QSPI_INIT(&gs_handle, w25qxx_interface_spi_qspi_init);
    DRIVER_W25QXX_LINK_SPI_QSPI_DEINIT(&gs_handle, w25qxx_interface_spi_qspi_deinit);
    DRIVER_W25QXX_LINK_SPI_QSPI_WRITE_READ(&gs_handle, w25qxx_interface_spi_qspi_write_read);
    DRIVER_W25QXX_LINK_DELAY_MS(&gs_handle, w25qxx_interface_delay_ms);
    DRIVER_W25QXX_LINK_DELAY_US(&gs_handle, w25qxx_interface_delay_us);
    DRIVER_W25QXX_LINK_DEBUG_PRINT(&gs_handle, w25qxx_interface_debug_print);

    /* set chip type */
    res = w25qxx_set_type(&gs_handle, W25Q64);
    if (res != 0)
    {
        w25qxx_interface_debug_print("w25qxx: set type failed.\n");

        return false;
    }

    /* set chip interface */
    res = w25qxx_set_interface(&gs_handle, W25QXX_INTERFACE_QSPI);
    if (res != 0)
    {
        w25qxx_interface_debug_print("w25qxx: set interface failed.\n");

        return false;
    }

    /* set dual quad spi */
    res = w25qxx_set_dual_quad_spi(&gs_handle, W25QXX_BOOL_TRUE);
    if (res != 0)
    {
        w25qxx_interface_debug_print("w25qxx: set dual quad spi failed.\n");
        (void)w25qxx_deinit(&gs_handle);

        return false;
    }

    /* chip init */
    res = w25qxx_init(&gs_handle);
    if (res != 0)
    {
        w25qxx_interface_debug_print("w25qxx: init failed.\n");

        return false;
    }

    return true;
}
