#include "CH58x_common.h"
#include "CH58xBLE_LIB.h"
#include "CONFIG.h"
#include "ble.h"

enum {
	SYSINFO_SVC_UUID = 0xFFE0,
	CHIPNAME_R_CHR_UUID = 0xFFE1,
	SYSCLOCK_RN_CHR_UUID = 0xFFE2,
	CHIPUID_R_CHR_UUID = 0xFFE3,
};

extern uint8_t chip_uid[8];

static const uint8_t SysInfoSvcUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(SYSINFO_SVC_UUID), HI_UINT16(SYSINFO_SVC_UUID)
};
static const gattAttrType_t SysInfoSvc = { ATT_BT_UUID_SIZE, SysInfoSvcUUID };

const uint8_t SysInfoSysClockUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(SYSCLOCK_RN_CHR_UUID), HI_UINT16(SYSCLOCK_RN_CHR_UUID)
};

const static uint8_t SysInfoSysClockProps = GATT_PROP_READ | GATT_PROP_NOTIFY;

static uint8_t SysInfoSysClockUserDesp[] = "System Clock unit 625us\0";

gattCharCfg_t SysInfoSysClockConfig[PERIPHERAL_MAX_CONNECTION];

const uint8_t SysInfoChipUidUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(CHIPUID_R_CHR_UUID), HI_UINT16(CHIPUID_R_CHR_UUID)
};

const static uint8_t SysInfoChipUidProps = GATT_PROP_READ;

static uint8_t SysInfoChipUidUserDesp[] = "chip uid\0";

gattAttribute_t SysInfoAttrTbl[] = {
	// System Information Service
	{
		{ ATT_BT_UUID_SIZE, primaryServiceUUID }, /* type */
		GATT_PERMIT_READ | GATT_PERMIT_WRITE, /* permissions */
		0, /* handle */
		(uint8_t *)&SysInfoSvc /* pValue */
	},

	// System Clock Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&SysInfoSysClockProps
	},

	// System Clock Value
	{
		{ ATT_BT_UUID_SIZE, SysInfoSysClockUUID },
		GATT_PERMIT_READ,
		0,
		NULL,
	},

	// System Clock User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		SysInfoSysClockUserDesp,
	},

	// System Clock Notify configuration
	{
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ | GATT_PERMIT_WRITE,
		0,
		(uint8_t *)SysInfoSysClockConfig,
	},

	// Chip Uid Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&SysInfoChipUidProps
	},

	// Chip Uid Value
	{
		{ ATT_BT_UUID_SIZE, SysInfoChipUidUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)chip_uid,
	},

	// Chip Uid User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		SysInfoChipUidUserDesp,
	},
};

static bStatus_t SysInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
				    uint8_t *pValue, uint16_t *pLen,
				    uint16_t offset, uint16_t maxLen,
				    uint8_t method)
{
	bStatus_t status = SUCCESS;
	uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
	if (uuid == SYSCLOCK_RN_CHR_UUID) {
		if (offset != 0) {
			status = ATT_ERR_ATTR_NOT_LONG;
			return status;
		}
		uint32_t sysclock = TMOS_GetSystemClock();
		*pLen = MIN(maxLen, sizeof(sysclock));
		tmos_memcpy(pValue, &sysclock, *pLen);
		return status;
	}

	if (uuid == CHIPUID_R_CHR_UUID) {
		// check offset
		if (offset >= sizeof(chip_uid)) {
			status = ATT_ERR_INVALID_OFFSET;
			return status;
		}
		// determine read length
		*pLen = MIN(maxLen, (sizeof(chip_uid) - offset));
		// copy data
		tmos_memcpy(pValue, &chip_uid[offset], *pLen);
		return status;
	}

	PERI_DBG_PRINT("%s: Unhandle UUID: 0x%04X\n\r", __func__, uuid);
	*pLen = 0;
	status = ATT_ERR_ATTR_NOT_FOUND;
	return status;
}

static bStatus_t SysInfo_WriteAttrCB(uint16_t connHandle,
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

bStatus_t SysInfoSysClock_Notify(uint16_t connHandle,
				 attHandleValueNoti_t *pNoti)
{
	uint16_t value =
		GATTServApp_ReadCharCfg(connHandle, SysInfoSysClockConfig);

	// If notifications enabled
	if (value & GATT_CLIENT_CFG_NOTIFY) {
		// Set the handle
		// C language not support label in array
		// we need compute this index by hand....
		pNoti->handle = SysInfoAttrTbl[2].handle;

		// Send the notification
		return GATT_Notification(connHandle, pNoti, FALSE);
	}
	return bleIncorrectMode;
}

void peripheralSysInfoSysClockNotify(uint16_t connHandle)
{
	uint16_t value =
		GATTServApp_ReadCharCfg(connHandle, SysInfoSysClockConfig);

	// If notifications disable
	if ((value & GATT_CLIENT_CFG_NOTIFY) == 0) {
		return;
	}

	attHandleValueNoti_t noti;
	uint32_t sysclock;
	sysclock = TMOS_GetSystemClock();
	noti.len = sizeof(sysclock);
	noti.pValue = GATT_bm_alloc(connHandle, ATT_HANDLE_VALUE_NOTI, noti.len,
				    NULL, 0);
	tmos_memcpy(noti.pValue, &sysclock, noti.len);
	if (SysInfoSysClock_Notify(connHandle, &noti) != SUCCESS) {
		GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
	}
}

static gattServiceCBs_t SysInfoCBs = {
	SysInfo_ReadAttrCB, // Read callback function pointer
	SysInfo_WriteAttrCB, // Write callback function pointer
	NULL // Authorization callback function pointer
};

bStatus_t GATT_AddSysInfo_Service(void) {
	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, SysInfoSysClockConfig);
	return GATTServApp_RegisterService(SysInfoAttrTbl,
				    GATT_NUM_ATTRS(SysInfoAttrTbl),
				    GATT_MAX_ENCRYPT_KEY_SIZE, &SysInfoCBs);
}
