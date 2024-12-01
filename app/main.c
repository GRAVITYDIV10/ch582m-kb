#include <stdarg.h>
#include "CH58xBLE_LIB.h"
#include "CONFIG.h"
#include "HAL.h"

#define DBG_PRINT(...) PRINT(__VA_ARGS__)

uint8_t chip_uid[8];
uint16_t chip_uid_sum = 0;

extern void Peripheral_Init(void);

__HIGH_CODE
__attribute__((noinline)) void Main_Circulation()
{
	while (1) {
		TMOS_SystemProcess();
	}
}

int main(void)
{
#if (defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
	PWR_DCDCCfg(ENABLE);
#endif
	SetSysClock(CLK_SOURCE_PLL_60MHz);
#if (defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
	GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
	GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif
#ifdef DEBUG
	GPIOA_SetBits(bTXD1);
	GPIOA_ModeCfg(bTXD1, GPIO_ModeOut_PP_5mA);
	UART1_DefInit();
#endif
	UART1_BaudRateCfg(921600);
	int i;
	for (i = 0; i < 128; i++) {
		UART1_SendByte(0x55);
	}
	UART1_SendByte('\n');
	UART1_SendByte('\r');
	DBG_PRINT("RUN ON CH5%02X\n\r", R8_CHIP_ID);
	DBG_PRINT("CHIP UID: ");
	GET_UNIQUE_ID(chip_uid);
	for (i = 0; i < 8; i++) {
		chip_uid_sum += chip_uid[i];
		DBG_PRINT("%02X", chip_uid[i]);
	}
	DBG_PRINT("\n\r");
	DBG_PRINT("%s\n\r", VER_LIB);
	CH58X_BLEInit();
	HAL_Init();
	GAPRole_PeripheralInit();
	GAPRole_CentralInit();
	Peripheral_Init();
	Main_Circulation();
}
