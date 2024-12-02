#include "CH58x_common.h"
#include "CH58xBLE_LIB.h"
#include "CONFIG.h"
#include "ble.h"
#include "fifo8.h"

enum {
	CONSOLE_SVC_UUID = 0xFFC0,
	CONSOLE_RNW_CHR_UUID = 0xFFC1,
	CONSOLE_CTL_CHR_UUID = 0xFFC2,
};

extern uint8_t chip_uid[8];

static const uint8_t ConsoleSvcUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(CONSOLE_SVC_UUID), HI_UINT16(CONSOLE_SVC_UUID)
};
static const gattAttrType_t ConsoleSvc = { ATT_BT_UUID_SIZE, ConsoleSvcUUID };

const uint8_t ConsoleRNWUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(CONSOLE_RNW_CHR_UUID), HI_UINT16(CONSOLE_RNW_CHR_UUID)
};

const static uint8_t ConsoleRNWProps =
	GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NO_RSP | GATT_PROP_NOTIFY;

const static uint8_t ConsoleRNWUserDesp[] = "Debug Console\0";

static gattCharCfg_t ConsoleRNWConfig[PERIPHERAL_MAX_CONNECTION];

const uint8_t ConsoleCtlUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(CONSOLE_CTL_CHR_UUID), HI_UINT16(CONSOLE_CTL_CHR_UUID)
};

const static uint8_t ConsoleCtlProps = GATT_PROP_READ;

const static uint8_t ConsoleCtlUserDesp[] = "Debug Console Info Interface, \
byte0 is rx fifo free,\
byte1 is tx fifo free \0";

static gattAttribute_t ConsoleAttrTbl[] = {
	// Console Service
	{
		{ ATT_BT_UUID_SIZE, primaryServiceUUID }, /* type */
		GATT_PERMIT_READ | GATT_PERMIT_WRITE, /* permissions */
		0, /* handle */
		(uint8_t *)&ConsoleSvc /* pValue */
	},

	// Console Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&ConsoleRNWProps
	},

	// Console Value
	{
		{ ATT_BT_UUID_SIZE, ConsoleRNWUUID },
		GATT_PERMIT_READ | GATT_PERMIT_WRITE,
		0,
		NULL,
	},

	// Console User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)ConsoleRNWUserDesp,
	},

	// Console Notify configuration
	{
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ | GATT_PERMIT_WRITE,
		0,
		(uint8_t *)ConsoleRNWConfig,
	},

	// Console Ctl Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&ConsoleCtlProps
	},

	// Console Ctl Value
	{
		{ ATT_BT_UUID_SIZE, ConsoleCtlUUID },
		GATT_PERMIT_READ,
		0,
		NULL,
	},

	// Console Ctl Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)ConsoleCtlUserDesp,
	},
};

extern struct ble_peri_slot ble_peri_slots[PERIPHERAL_MAX_CONNECTION];

static bStatus_t Console_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
				    uint8_t *pValue, uint16_t *pLen,
				    uint16_t offset, uint16_t maxLen,
				    uint8_t method)
{
	bStatus_t status = SUCCESS;
	uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	if (slotp < 0) {
		PERI_PANIC();
		return ATT_ERR_INVALID_PDU;
	}
	
	if (uuid == CONSOLE_RNW_CHR_UUID) {
		if (offset != 0) {
			status = ATT_ERR_ATTR_NOT_LONG;
			return status;
		}
		*pLen = MIN(maxLen, fifo8_used(&ble_peri_slots[slotp].contx_fifo));
		for (int i = 0; i < *pLen; i++) {
			pValue[i] = fifo8_pop(&ble_peri_slots[slotp].contx_fifo);
		}
		return status;
	}

	if (uuid == CONSOLE_CTL_CHR_UUID) {
		if (offset != 0) {
			status = ATT_ERR_ATTR_NOT_LONG;
			return status;
		}
		*pLen = 2;
		pValue[0] = fifo8_free(&ble_peri_slots[slotp].conrx_fifo);
		pValue[1] = fifo8_free(&ble_peri_slots[slotp].contx_fifo);
		return status;
	}

	PERI_DBG_PRINT("%s: Unhandle UUID: 0x%04X\n\r", __func__, uuid);
	*pLen = 0;
	status = ATT_ERR_ATTR_NOT_FOUND;
	return status;
}

static bStatus_t Console_WriteAttrCB(uint16_t connHandle,
				     gattAttribute_t *pAttr, uint8_t *pValue,
				     uint16_t len, uint16_t offset,
				     uint8_t method)
{
	bStatus_t status = SUCCESS;
	uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	if (slotp < 0) {
		PERI_PANIC();
		return ATT_ERR_INVALID_PDU;
	}
	if (uuid == CONSOLE_RNW_CHR_UUID) {
		int i = 0;
		while (len && fifo8_free(&ble_peri_slots[slotp].conrx_fifo)) {
			fifo8_push(&ble_peri_slots[slotp].conrx_fifo, pValue[i]);
			len--; i++;
		}
		return status;
	}

	if (uuid == GATT_CLIENT_CHAR_CFG_UUID) {
		status = GATTServApp_ProcessCCCWriteReq(connHandle, pAttr,
							pValue, len, offset,
							GATT_CLIENT_CFG_NOTIFY);
		return status;
	}

	PERI_DBG_PRINT("%s: Unhandle UUID: 0x%04X\n\r", __func__, uuid);
	status = ATT_ERR_ATTR_NOT_FOUND;
	return status;
}

bStatus_t ConsoleRNW_Notify(uint16_t connHandle,
				 attHandleValueNoti_t *pNoti)
{
	uint16_t value =
		GATTServApp_ReadCharCfg(connHandle, ConsoleRNWConfig);

	// If notifications enabled
	if (value & GATT_CLIENT_CFG_NOTIFY) {
		// Set the handle
		// C language not support label in array
		// we need compute this index by hand....
		pNoti->handle = ConsoleAttrTbl[2].handle;

		// Send the notification
		return GATT_Notification(connHandle, pNoti, FALSE);
	}
	return bleIncorrectMode;
}

void peripheralConsoleRNWNotify(uint16_t connHandle)
{
	uint16_t value =
		GATTServApp_ReadCharCfg(connHandle, ConsoleRNWConfig);

	// If notifications disable
	if ((value & GATT_CLIENT_CFG_NOTIFY) == 0) {
		return;
	}

	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	if (slotp < 0) {
		PERI_PANIC();
		return;
	}

	attHandleValueNoti_t noti;
	int used;
	used = fifo8_used(&ble_peri_slots[slotp].contx_fifo);
	noti.len = MIN((ATT_MTU_SIZE - 4), used);
	if (noti.len == 0) {
		return;
	}
	noti.pValue = GATT_bm_alloc(connHandle, ATT_HANDLE_VALUE_NOTI, noti.len,
				    NULL, 0);
	int i;
	for (i = 0; i < noti.len; i++) {
		noti.pValue[i] = fifo8_pop(&ble_peri_slots[slotp].contx_fifo);
	}
	if (ConsoleRNW_Notify(connHandle, &noti) != SUCCESS) {
		GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
	}
}

static gattServiceCBs_t ConsoleCBs = {
	Console_ReadAttrCB, // Read callback function pointer
	Console_WriteAttrCB, // Write callback function pointer
	NULL // Authorization callback function pointer
};

bStatus_t GATT_AddConsole_Service(void) {
	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, ConsoleRNWConfig);
	return GATTServApp_RegisterService(ConsoleAttrTbl,
				    GATT_NUM_ATTRS(ConsoleAttrTbl),
				    GATT_MAX_ENCRYPT_KEY_SIZE, &ConsoleCBs);
}
