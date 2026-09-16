#include "test.h"
#include "main.h"
#include "i2s.h"
#include "log_debug.h"
#include "typedef.h"

const s16 sin_48k[48] = {
    0, 2139, 4240, 6270, 8192, 9974, 11585, 12998,
    14189, 15137, 15826, 16244, 16384, 16244, 15826, 15137,
    14189, 12998, 11585, 9974, 8192, 6270, 4240, 2139,
    0, -2139, -4240, -6270, -8192, -9974, -11585, -12998,
    -14189, -15137, -15826, -16244, -16384, -16244, -15826, -15137,
    -14189, -12998, -11585, -9974, -8192, -6270, -4240, -2139
};

// const s16 sin_48k[48] = {
// 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555,
// 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555,
// 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555,
// 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555,
// 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555,
// 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555, 0X5555,
// };
    
int get_sine48k_data(u16 *s_cnt, s16 *data, u16 points, u8 ch)
{
    while (points--) {
        if (*s_cnt >= 48) {
            *s_cnt = 0;
        }
        *data++ = sin_48k[*s_cnt];
        if (ch == 2) {
            *data++ = sin_48k[*s_cnt];
        }
        (*s_cnt)++;
    }
    return 0;
}

static u16 tx_s_cnt = 0;
int16_t tx_buffer[48 * 2];

static void iis_tx_irq_callback(void *data, uint16_t len)
{
    get_sine48k_data(&tx_s_cnt, data, len/4, 2);
}
void iis_tx_test(void)
{
	log_debug("iis_tx\n");
	// get_sine48k_data(&tx_s_cnt, tx_buffer, 48, 2);
	// HAL_I2S_Transmit_DMA(&hi2s2, tx_buffer, 48 * 2);
    HAL_I2s_tx_start(iis_tx_irq_callback);
}
