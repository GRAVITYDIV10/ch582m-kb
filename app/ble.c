#include <stdarg.h>
#include "CH58xBLE_LIB.h"
#include "CONFIG.h"
#include "HAL.h"

#define DBG_PRINT(...) PRINT(__VA_ARGS__)

#define PERI_DBG_PRINT(...)             \
	{                               \
		DBG_PRINT("BLE PERI:"); \
		DBG_PRINT(__VA_ARGS__); \
	}

enum {
	TEST_SVC_UUID = 0xFF00,
	PAD_R_CHR_UUID = 0xFF01,
	PAD_W_CHR_UUID = 0xFF02,
	PAD_W_NO_RSP_CHR_UUID = 0xFF03,
	PAD_N_CHR_UUID = 0xFF04,
	PAD_SUM_CHR_UUID = 0xFF05,
	PAD_RAND_CHR_UUID = 0xFF06,

	SYSINFO_SVC_UUID = 0xFF10,
	CHIPNAME_R_CHR_UUID = 0xFF11,
	SYSCLOCK_RN_CHR_UUID = 0xFF12,
};

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
extern uint8_t chip_uid[8];
extern uint16_t chip_uid_sum;

enum {
	PAD_BUF_SIZE = 96,
};

struct {
	uint16_t state;
	uint8_t taskID;
	uint16_t connHandle;
	uint32_t periodic_cnt;
	uint32_t periodic_delay;
	uint16_t padused;
	uint8_t padbuf[PAD_BUF_SIZE];
} ble_peri_slots[PERIPHERAL_MAX_CONNECTION];

enum {
	STATE_DEV_CONNECTED = (1 << 0),
};

static int ble_peri_slots_find_free(void)
{
	int slotp;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].state == 0) {
			return slotp;
		}
	}
	return -1;
}

static int ble_peri_slots_is_full(void)
{
	return (ble_peri_slots_find_free() < 0);
}

static int ble_peri_slots_used(void)
{
	int slotp;
	int count = 0;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].state != 0) {
			count++;
		}
	}
	return count;
}

static int ble_peri_slots_free(void)
{
	return (PERIPHERAL_MAX_CONNECTION - ble_peri_slots_used());
}

static int ble_peri_slots_find_by_connHandle(int connHandle)
{
	int slotp;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].connHandle == connHandle) {
			return slotp;
		}
	}
	return -1;
}

static int ble_peri_slots_find_by_taskID(int task_id)
{
	int slotp;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].taskID == task_id) {
			return slotp;
		}
	}
	return -1;
}

enum {
	CONNECTION_INTERVAL_MIN = 9, // x 1.25ms =  11.25ms
	CONNECTION_INTERVAL_MAX = 100, // x 1.25ms = 125ms
	CONNECTION_TIMEOUT = 100, // x 0.1ms = 10ms
	SLAVE_LATENCY = 0,
	ADVERTISING_INTERVAL_MIN = 80, // x 0.625 = 50ms
	ADVERTISING_INTERVAL_MAX = 160, // x 0.625 = 100ms
	PARAM_UPDATE_DELAY = 6400,
	PHY_UPDATE_DELAY = 3200,
	PERIOD_READ_RSSI = 3200,
	PERIODIC_PERIOD = 1000,
};

static uint8_t Peripheral_TaskID = INVALID_TASK_ID;

// Peripheral Task Events
enum {
	SBP_START_DEVICE_EVT = (1 << 0),
	SBP_PERIODIC_EVT = (1 << 1),
	SBP_READ_RSSI_EVT = (1 << 2),
	SBP_PARAM_UPDATE_EVT = (1 << 3),
	SBP_PHY_UPDATE_EVT = (1 << 4),
};

static void Peripheral_LinkEstablished(gapRoleEvent_t *pEvent)
{
	PERI_DBG_PRINT("Connected\n\r");
	if (ble_peri_slots_is_full()) {
		PERI_DBG_PRINT(
			"connection slots is full, drop new connection\n\r");
		GAPRole_TerminateLink(pEvent->linkCmpl.connectionHandle);
		return;
	}
	int slotp;
	slotp = ble_peri_slots_find_free();
	ble_peri_slots[slotp].state |= STATE_DEV_CONNECTED;
	ble_peri_slots[slotp].connHandle = pEvent->linkCmpl.connectionHandle;
	PERI_DBG_PRINT("slots used: %d\n\r", ble_peri_slots_used());
	PERI_DBG_PRINT("slots free: %d\n\r", ble_peri_slots_free());

	// Set timer for param update event
	tmos_start_task(ble_peri_slots[slotp].taskID, SBP_PARAM_UPDATE_EVT,
			PARAM_UPDATE_DELAY);

	// Set timer for phy update event
	tmos_start_task(ble_peri_slots[slotp].taskID, SBP_PHY_UPDATE_EVT,
			PHY_UPDATE_DELAY);

	// Start read rssi
	tmos_start_task(ble_peri_slots[slotp].taskID, SBP_READ_RSSI_EVT,
			PERIOD_READ_RSSI);
}

#define PANIC()                                                              \
	{                                                                    \
		PERI_DBG_PRINT("%s: PANIC LINE %d\n\r", __func__, __LINE__); \
	}

static void Peripheral_LinkTerminated(gapRoleEvent_t *pEvent)
{
	PERI_DBG_PRINT("Disconnected...Reason: 0x%02X\n\r",
		       pEvent->linkTerminate.reason);
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(
		pEvent->linkTerminate.connectionHandle);
	if (slotp < 0) {
		PANIC();
		return;
	}
	// Stop timer for periodic event
	tmos_stop_task(ble_peri_slots[slotp].taskID, SBP_PERIODIC_EVT);

	ble_peri_slots[slotp].state = 0;
	ble_peri_slots[slotp].connHandle = GAP_CONNHANDLE_INIT;
	PERI_DBG_PRINT("slots used: %d\n\r", ble_peri_slots_used());
	PERI_DBG_PRINT("slots free: %d\n\r", ble_peri_slots_free());
	if (ble_peri_slots_free() > 0) {
		// Restart advertising
		PERI_DBG_PRINT("Advertising.. \n\r");
		uint8_t advertising_enable = TRUE;
		GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
				     &advertising_enable);
	}
}

static void peripheralStateNotificationCB(gapRole_States_t newState,
					  gapRoleEvent_t *pEvent)
{
	switch (newState & GAPROLE_STATE_ADV_MASK) {
	case GAPROLE_STARTED:
		PERI_DBG_PRINT("Initialized...\n\r");
		break;
	case GAPROLE_ADVERTISING:
		if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
			Peripheral_LinkTerminated(pEvent);
			PERI_DBG_PRINT("Advertising...\n\r");
		}
		if (pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT) {
			PERI_DBG_PRINT("Device Discoverable\n\r");
			PERI_DBG_PRINT("Advertising...\n\r");
		}
		break;
	case GAPROLE_WAITING:
		if (pEvent->gap.opcode == GAP_END_DISCOVERABLE_DONE_EVENT) {
			PERI_DBG_PRINT("Waiting for advertising..\n\r");
		} else if (pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
			Peripheral_LinkTerminated(pEvent);
		} else if (pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT) {
			if (pEvent->gap.hdr.status != SUCCESS) {
				PERI_DBG_PRINT("Waiting for advertising..\n\r");
			} else {
				PERI_DBG_PRINT("Error..\n\r");
			}
		} else {
			PERI_DBG_PRINT("Error..%x\n\r", pEvent->gap.opcode);
		}
		break;
	case GAPROLE_CONNECTED:
		Peripheral_LinkEstablished(pEvent);
		break;
	default:
		PERI_DBG_PRINT("State Not Handle: %08X\n\r",
			       (unsigned)newState);
		break;
	}
}

static void peripheralRssiCB(uint16_t connHandle, int8_t rssi)
{
	//PERI_DBG_PRINT("RSSI -%d dB Conn  %x \r\n", -rssi, connHandle);
}

static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
				    uint16_t connSlaveLatency,
				    uint16_t connTimeout)
{
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	PERI_DBG_PRINT("Slot %d Update Connection Interval = %d \n\r", slotp,
		       connInterval);
	if ((connInterval < CONNECTION_INTERVAL_MIN) ||
	    (connInterval > CONNECTION_INTERVAL_MAX)) {
		// if connect interval not between with the define scope,
		// Set timer for param update event
		tmos_start_task(ble_peri_slots[slotp].taskID,
				SBP_PARAM_UPDATE_EVT, PARAM_UPDATE_DELAY);
	}

	// Set timer for periodic event
	ble_peri_slots[slotp].periodic_delay = connInterval * 2 + 1;
	tmos_start_task(ble_peri_slots[slotp].taskID, SBP_PERIODIC_EVT,
			ble_peri_slots[slotp].periodic_delay);
}

static gapRolesCBs_t Peripheral_PeripheralCBs = {
	// Profile State Change Callbacks
	peripheralStateNotificationCB,
	// When a valid RSSI is read from controller (not used by application)
	peripheralRssiCB, peripheralParamUpdateCB
};

static gapRolesBroadcasterCBs_t Broadcaster_BroadcasterCBs = {
	NULL, // Not used in peripheral role
	NULL // Receive scan request callback
};

// GAP Bond Manager Callbacks
static gapBondCBs_t Peripheral_BondMgrCBs = {
	NULL, // Passcode callback (not used by application)
	NULL, // Pairing / Bonding state Callback (not used by application)
	NULL // oob callback
};

static void Peripheral_ProcessGAPMsg(gapRoleEvent_t *pEvent)
{
	switch (pEvent->gap.opcode) {
	case GAP_SCAN_REQUEST_EVENT:
		/*
		PERI_DBG_PRINT("Receive scan req from %02X %02X %02X %02X %02X %02X  ..\r\n",
			       pEvent->scanReqEvt.scannerAddr[0],
			       pEvent->scanReqEvt.scannerAddr[1],
			       pEvent->scanReqEvt.scannerAddr[2],
			       pEvent->scanReqEvt.scannerAddr[3],
			       pEvent->scanReqEvt.scannerAddr[4],
			       pEvent->scanReqEvt.scannerAddr[5]);
		*/
		break;

	case GAP_PHY_UPDATE_EVENT:
		PERI_DBG_PRINT("Phy update Rx:%x Tx:%x ..\n\r",
			       pEvent->linkPhyUpdate.connRxPHYS,
			       pEvent->linkPhyUpdate.connTxPHYS);
		break;

	default:
		break;
	}
}

static uint8_t peripheralMTU = ATT_MTU_SIZE;

static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
	switch (pMsg->event) {
	case GAP_MSG_EVENT: {
		Peripheral_ProcessGAPMsg((gapRoleEvent_t *)pMsg);
		break;
	}

	case GATT_MSG_EVENT: {
		gattMsgEvent_t *pMsgEvent;

		pMsgEvent = (gattMsgEvent_t *)pMsg;
		if (pMsgEvent->method == ATT_MTU_UPDATED_EVENT) {
			peripheralMTU =
				pMsgEvent->msg.exchangeMTUReq.clientRxMTU;
			PERI_DBG_PRINT(
				"mtu exchange: %d\n\r",
				pMsgEvent->msg.exchangeMTUReq.clientRxMTU);
		}
		break;
	}

	default:
		break;
	}
}

// GAP GATT Attributes
static char attDeviceName[GAP_DEVICE_NAME_LEN] = "CH582-0000";

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scanRspData[] = {
	// complete name
	0xC, // length of this data
	GAP_ADTYPE_LOCAL_NAME_COMPLETE, 'C', 'H', '5', '8', '2', '-', '0', '0',
	'0', '0', 0x0,
	// connection interval range
	0x05, // length of this data
	GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
	LO_UINT16(CONNECTION_INTERVAL_MIN), HI_UINT16(CONNECTION_INTERVAL_MIN),
	LO_UINT16(CONNECTION_INTERVAL_MAX), HI_UINT16(CONNECTION_INTERVAL_MAX),
	// Tx power level
	0x02, // length of this data
	GAP_ADTYPE_POWER_LEVEL,
	0 // 0dBm
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertising)
static uint8_t advertData[] = {
	0x02, // length of this data
	GAP_ADTYPE_FLAGS,
	GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
};

static const uint8_t TestSvcUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(TEST_SVC_UUID), HI_UINT16(TEST_SVC_UUID)
};
static const gattAttrType_t TestSvc = { ATT_BT_UUID_SIZE, TestSvcUUID };

const uint8_t TestPadrUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(PAD_R_CHR_UUID),
						 HI_UINT16(PAD_R_CHR_UUID) };

const static uint8_t TestPadrProps = GATT_PROP_READ;

static uint8_t TestPadrUserDesp[] = "read from pad buffer\0";

const uint8_t TestPadwUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(PAD_W_CHR_UUID),
						 HI_UINT16(PAD_W_CHR_UUID) };

const static uint8_t TestPadwProps = GATT_PROP_WRITE;

static uint8_t TestPadwUserDesp[] = "write data to pad buffer\0";

const uint8_t TestPadwnorspUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(PAD_W_NO_RSP_CHR_UUID), HI_UINT16(PAD_W_NO_RSP_CHR_UUID)
};

const static uint8_t TestPadwnorspProps = GATT_PROP_WRITE_NO_RSP;

static uint8_t TestPadwnorspUserDesp[] =
	"write data to pad buffer, no response\0";

const uint8_t TestPadnUUID[ATT_BT_UUID_SIZE] = { LO_UINT16(PAD_N_CHR_UUID),
						 HI_UINT16(PAD_N_CHR_UUID) };

const static uint8_t TestPadnProps = GATT_PROP_NOTIFY;

static uint8_t TestPadnUserDesp[] =
	"when pad buffer change, send notification\0";

static gattCharCfg_t TestPadnConfig[PERIPHERAL_MAX_CONNECTION];

const uint8_t TestPadsumUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(PAD_SUM_CHR_UUID), HI_UINT16(PAD_SUM_CHR_UUID)
};

const static uint8_t TestPadsumProps = GATT_PROP_READ;

static uint8_t TestPadsumUserDesp[] = "compute & read pad buffer sum\0";

const uint8_t TestPadrandUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(PAD_RAND_CHR_UUID), HI_UINT16(PAD_RAND_CHR_UUID)
};

const static uint8_t TestPadrandProps = GATT_PROP_READ;

static uint8_t TestPadrandUserDesp[] = "use random number fill pad buffer\0";

static gattAttribute_t TestAttrTbl[] = {
	// Test Service
	{
		{ ATT_BT_UUID_SIZE, primaryServiceUUID }, /* type */
		GATT_PERMIT_READ | GATT_PERMIT_WRITE, /* permissions */
		0, /* handle */
		(uint8_t *)&TestSvc /* pValue */
	},

	// Pad Read Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&TestPadrProps,
	},

	// Pad Read Value
	{
		{ ATT_BT_UUID_SIZE, TestPadrUUID },
		GATT_PERMIT_READ,
		0,
		NULL,
	},

	// Pad Read User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		TestPadrUserDesp,
	},

	// Pad Write Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&TestPadwProps,
	},

	// Pad Write Value
	{
		{ ATT_BT_UUID_SIZE, TestPadwUUID },
		GATT_PERMIT_WRITE,
		0,
		NULL,
	},

	// Pad Write User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		TestPadwUserDesp,
	},

	// Pad Write no rsp Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&TestPadwnorspProps,
	},

	// Pad Write no rsp Value
	{
		{ ATT_BT_UUID_SIZE, TestPadwnorspUUID },
		GATT_PERMIT_WRITE,
		0,
		NULL,
	},

	// Pad Write no rsp User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		TestPadwnorspUserDesp,
	},

	// Pad Notify Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&TestPadnProps,
	},

	// Pad Notify Value
	{
		{ ATT_BT_UUID_SIZE, TestPadnUUID },
		GATT_PERMIT_READ,
		0,
		NULL,
	},

	// Pad Notify User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		TestPadnUserDesp,
	},

	// Pad Notify configuration
	{
		{ ATT_BT_UUID_SIZE, clientCharCfgUUID },
		GATT_PERMIT_READ | GATT_PERMIT_WRITE,
		0,
		(uint8_t *)TestPadnConfig,
	},

	// Pad Sum Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&TestPadsumProps,
	},

	// Pad Sum Value
	{
		{ ATT_BT_UUID_SIZE, TestPadsumUUID },
		GATT_PERMIT_READ,
		0,
		NULL,
	},

	// Pad Sum User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		TestPadsumUserDesp,
	},

	// Pad Rand Declaration
	{
		{ ATT_BT_UUID_SIZE, characterUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)&TestPadrandProps,
	},

	// Pad Rand Value
	{
		{ ATT_BT_UUID_SIZE, TestPadrandUUID },
		GATT_PERMIT_READ,
		0,
		NULL,
	},

	// Pad Rand User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		TestPadrandUserDesp,
	},
};

static bStatus_t Test_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
				 uint8_t *pValue, uint16_t *pLen,
				 uint16_t offset, uint16_t maxLen,
				 uint8_t method)
{
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	if (slotp < 0) {
		PANIC();
		return ATT_ERR_INVALID_PDU;
	}

	bStatus_t status = SUCCESS;
	uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
	int i;

	if (uuid == PAD_R_CHR_UUID) {
		// check len & offset
		if (ble_peri_slots[slotp].padused == 0) {
			*pLen = 0;
			return status;
		}
		if (offset >= ble_peri_slots[slotp].padused) {
			return ATT_ERR_INVALID_OFFSET;
		}
		// determine read length
		*pLen = MIN(maxLen, ble_peri_slots[slotp].padused - offset);
		// copy data
		tmos_memcpy(pValue, &ble_peri_slots[slotp].padbuf[offset],
			    *pLen);
		return status;
	}

	if (uuid == PAD_SUM_CHR_UUID) {
		uint8_t sum = 0x0;
		*pLen = sizeof(sum);

		for (i = 0; i < ble_peri_slots[slotp].padused; i++) {
			sum += ble_peri_slots[slotp].padbuf[i];
		}
		tmos_memcpy(pValue, &sum, *pLen);
		return status;
	}

	if (uuid == PAD_RAND_CHR_UUID) {
		ble_peri_slots[slotp].padused = tmos_rand() % PAD_BUF_SIZE;
		for (i = 0; i < ble_peri_slots[slotp].padused; i++) {
			ble_peri_slots[slotp].padbuf[i] = tmos_rand();
		}
		*pLen = sizeof(ble_peri_slots[slotp].padused);
		tmos_memcpy(pValue, &ble_peri_slots[slotp].padused, *pLen);
		return status;
	}

	PERI_DBG_PRINT("%s: Unhandle UUID: 0x%04X\n\r", __func__, uuid);
	*pLen = 0;
	status = ATT_ERR_ATTR_NOT_FOUND;
	return status;
}

bStatus_t TestPadn_Notify(uint16_t connHandle, attHandleValueNoti_t *pNoti)
{
	uint16_t value = GATTServApp_ReadCharCfg(connHandle, TestPadnConfig);

	// If notifications enabled
	if (value & GATT_CLIENT_CFG_NOTIFY) {
		// Set the handle
		// C language not support label in array
		// we need compute this index by hand....
		pNoti->handle = TestAttrTbl[11].handle;

		// Send the notification
		return GATT_Notification(connHandle, pNoti, FALSE);
	}
	return bleIncorrectMode;
}

static void peripheralTestPadnNotify(uint16_t connHandle, uint8_t *pValue,
				     uint16_t len)
{
	attHandleValueNoti_t noti;
	if (len > (peripheralMTU - 3)) {
		PERI_DBG_PRINT("Too large noti\n");
		return;
	}
	noti.len = len;
	noti.pValue = GATT_bm_alloc(connHandle, ATT_HANDLE_VALUE_NOTI, noti.len,
				    NULL, 0);
	tmos_memcpy(noti.pValue, pValue, noti.len);
	if (TestPadn_Notify(connHandle, &noti) != SUCCESS) {
		PANIC();
		GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
	}
}

static bStatus_t Test_WriteAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
				  uint8_t *pValue, uint16_t len,
				  uint16_t offset, uint8_t method)
{
	bStatus_t status = SUCCESS;
	uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);

	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	if (slotp < 0) {
		PANIC();
		return ATT_ERR_INVALID_PDU;
	}

	if ((uuid == PAD_W_CHR_UUID) || (uuid == PAD_W_NO_RSP_CHR_UUID)) {
		if (offset > 0) {
			status = ATT_ERR_ATTR_NOT_LONG;
		}
		if (len > PAD_BUF_SIZE) {
			status = ATT_ERR_INVALID_VALUE;
		}
		if (status == SUCCESS) {
			ble_peri_slots[slotp].padused = len;
			tmos_memcpy(ble_peri_slots[slotp].padbuf, pValue, len);
			peripheralTestPadnNotify(connHandle, pValue, len);
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

static gattServiceCBs_t TestCBs = {
	Test_ReadAttrCB, // Read callback function pointer
	Test_WriteAttrCB, // Write callback function pointer
	NULL // Authorization callback function pointer
};

static const uint8_t SysInfoSvcUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(SYSINFO_SVC_UUID), HI_UINT16(SYSINFO_SVC_UUID)
};
static const gattAttrType_t SysInfoSvc = { ATT_BT_UUID_SIZE, SysInfoSvcUUID };

const uint8_t SysInfoChipNameUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(CHIPNAME_R_CHR_UUID), HI_UINT16(CHIPNAME_R_CHR_UUID)
};

const static uint8_t SysInfoChipNameProps = GATT_PROP_READ;

static uint8_t SysInfoChipNameUserDesp[] = "chip name\0";

static char SysInfoChipName[] = "CH5XX";

const uint8_t SysInfoSysClockUUID[ATT_BT_UUID_SIZE] = {
	LO_UINT16(SYSCLOCK_RN_CHR_UUID), HI_UINT16(SYSCLOCK_RN_CHR_UUID)
};

const static uint8_t SysInfoSysClockProps = GATT_PROP_READ | GATT_PROP_NOTIFY;

static uint8_t SysInfoSysClockUserDesp[] = "System Clock unit 625us\0";

static gattCharCfg_t SysInfoSysClockConfig[PERIPHERAL_MAX_CONNECTION];

static gattAttribute_t SysInfoAttrTbl[] = {
	// System Information Service
	{
		{ ATT_BT_UUID_SIZE, primaryServiceUUID }, /* type */
		GATT_PERMIT_READ | GATT_PERMIT_WRITE, /* permissions */
		0, /* handle */
		(uint8_t *)&SysInfoSvc /* pValue */
	},

	// Chip Name Declaration
	{ { ATT_BT_UUID_SIZE, characterUUID },
	  GATT_PERMIT_READ,
	  0,
	  (uint8_t *)&SysInfoChipNameProps },

	// Chip Name Value
	{
		{ ATT_BT_UUID_SIZE, SysInfoChipNameUUID },
		GATT_PERMIT_READ,
		0,
		(uint8_t *)SysInfoChipName,
	},

	// Chip Name User Description
	{
		{ ATT_BT_UUID_SIZE, charUserDescUUID },
		GATT_PERMIT_READ,
		0,
		SysInfoChipNameUserDesp,
	},

	// System Clock Declaration
	{ { ATT_BT_UUID_SIZE, characterUUID },
	  GATT_PERMIT_READ,
	  0,
	  (uint8_t *)&SysInfoSysClockProps },

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

};

static bStatus_t SysInfo_ReadAttrCB(uint16_t connHandle, gattAttribute_t *pAttr,
				    uint8_t *pValue, uint16_t *pLen,
				    uint16_t offset, uint16_t maxLen,
				    uint8_t method)
{
	bStatus_t status = SUCCESS;
	uint16_t uuid = BUILD_UINT16(pAttr->type.uuid[0], pAttr->type.uuid[1]);
	if (uuid == CHIPNAME_R_CHR_UUID) {
		// check offset
		if (offset >= sizeof(SysInfoChipName)) {
			status = ATT_ERR_INVALID_OFFSET;
			return status;
		}
		// determine read length
		*pLen = MIN(maxLen, (sizeof(SysInfoChipName) - offset));
		// copy data
		tmos_memcpy(pValue, &SysInfoChipName[offset], *pLen);
		return status;
	}
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
		PANIC();
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
		pNoti->handle = TestAttrTbl[11].handle;

		// Send the notification
		return GATT_Notification(connHandle, pNoti, FALSE);
	}
	return bleIncorrectMode;
}

static void peripheralSysInfoSysClockNotify(uint16_t connHandle)
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

static void performPeriodicTask(uint16_t connHandle)
{
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	ble_peri_slots[slotp].periodic_cnt++;

	peripheralSysInfoSysClockNotify(connHandle);
}

static uint16_t peri_connect_ProcessEvent(uint8_t task_id, uint16_t events)
{
	int slotp;
	slotp = ble_peri_slots_find_by_taskID(task_id);
	if (slotp < 0) {
		PANIC();
		return 0;
	}

	if (events & SBP_PARAM_UPDATE_EVT) {
		PERI_DBG_PRINT("Send connection param update request\n\r");
		// Send connect param update request
		GAPRole_PeripheralConnParamUpdateReq(
			ble_peri_slots[slotp].connHandle,
			CONNECTION_INTERVAL_MIN, CONNECTION_INTERVAL_MAX,
			SLAVE_LATENCY, CONNECTION_TIMEOUT,
			ble_peri_slots[slotp].taskID);
		return (events ^ SBP_PARAM_UPDATE_EVT);
	}

	if (events & SBP_PHY_UPDATE_EVT) {
		// start phy update
		PERI_DBG_PRINT(
			"PHY Update %x...\n\r",
			GAPRole_UpdatePHY(ble_peri_slots[slotp].connHandle, 0,
					  GAP_PHY_BIT_LE_2M, GAP_PHY_BIT_LE_2M,
					  GAP_PHY_OPTIONS_NOPRE));
		return (events ^ SBP_PHY_UPDATE_EVT);
	}

	if (events & SBP_READ_RSSI_EVT) {
		GAPRole_ReadRssiCmd(ble_peri_slots[slotp].connHandle);
		tmos_start_task(ble_peri_slots[slotp].taskID, SBP_READ_RSSI_EVT,
				PERIOD_READ_RSSI);
		return (events ^ SBP_READ_RSSI_EVT);
	}

	if (events & SBP_PERIODIC_EVT) {
		// Restart timer
		if (ble_peri_slots[slotp].periodic_delay) {
			tmos_start_task(ble_peri_slots[slotp].taskID,
					SBP_PERIODIC_EVT,
					ble_peri_slots[slotp].periodic_delay);
		}
		// Perform periodic application task
		performPeriodicTask(ble_peri_slots[slotp].connHandle);
		return (events ^ SBP_PERIODIC_EVT);
	}

	PERI_DBG_PRINT("%s: unhandle events: 0x%02X\n\r", __func__, events);
	return 0;
}

static uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
	if (events & SYS_EVENT_MSG) {
		uint8_t *pMsg;

		if ((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL) {
			Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
			// Release the TMOS message
			tmos_msg_deallocate(pMsg);
		}
		// return unprocessed events
		return (events ^ SYS_EVENT_MSG);
	}

	if (events & SBP_START_DEVICE_EVT) {
		// Start the Device
		GAPRole_PeripheralStartDevice(Peripheral_TaskID,
					      &Peripheral_BondMgrCBs,
					      &Peripheral_PeripheralCBs);
		return (events ^ SBP_START_DEVICE_EVT);
	}

	return peri_connect_ProcessEvent(task_id, events);
}

static gattServiceCBs_t SysInfoCBs = {
	SysInfo_ReadAttrCB, // Read callback function pointer
	SysInfo_WriteAttrCB, // Write callback function pointer
	NULL // Authorization callback function pointer
};

void Peripheral_Init(void)
{
	PERI_DBG_PRINT("BLE Peripheral Init...\n\r");
	snprintf(attDeviceName, GAP_DEVICE_NAME_LEN - 1, "CH5%02X-%04X",
		 R8_CHIP_ID, chip_uid_sum);
	memcpy(&scanRspData[2], attDeviceName, MIN(10, strlen(attDeviceName)));
	PERI_DBG_PRINT("Device Name: %s\n\r", attDeviceName);

	memcpy(&SysInfoChipName[0], attDeviceName, 5);

	Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

	int slotp;
	memset(ble_peri_slots, 0x0, sizeof(ble_peri_slots));
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		ble_peri_slots[slotp].taskID =
			TMOS_ProcessEventRegister(Peripheral_ProcessEvent);
		ble_peri_slots[slotp].connHandle = GAP_CONNHANDLE_INIT;
	}

	uint8_t initial_advertising_enable = TRUE;
	uint16_t desired_min_interval = CONNECTION_INTERVAL_MIN;
	uint16_t desired_max_interval = CONNECTION_INTERVAL_MAX;
	GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t),
			     &initial_advertising_enable);
	GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData),
			     scanRspData);
	GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData),
			     advertData);
	GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t),
			     &desired_min_interval);
	GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t),
			     &desired_max_interval);

	// Set advertising interval
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, ADVERTISING_INTERVAL_MIN);
	GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, ADVERTISING_INTERVAL_MAX);

	// Enable scan req notify
	GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, ENABLE);

	// Register receive scan request callback
	GAPRole_BroadcasterSetCB(&Broadcaster_BroadcasterCBs);

	GGS_AddService(GATT_ALL_SERVICES); // GAP
	GATTServApp_AddService(GATT_ALL_SERVICES); // GATT attributes

	// Register GATT attribute list and CBs with GATT Server App
	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, TestPadnConfig);
	GATTServApp_RegisterService(TestAttrTbl, GATT_NUM_ATTRS(TestAttrTbl),
				    GATT_MAX_ENCRYPT_KEY_SIZE, &TestCBs);

	GATTServApp_InitCharCfg(INVALID_CONNHANDLE, SysInfoSysClockConfig);
	GATTServApp_RegisterService(SysInfoAttrTbl,
				    GATT_NUM_ATTRS(SysInfoAttrTbl),
				    GATT_MAX_ENCRYPT_KEY_SIZE, &SysInfoCBs);

	// Set the GAP Characteristics
	GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName),
			 attDeviceName);

	// Update Connection Params
	gapPeriConnectParams_t ConnectParams;
	ConnectParams.intervalMin = CONNECTION_INTERVAL_MIN;
	ConnectParams.intervalMax = CONNECTION_INTERVAL_MAX;
	ConnectParams.latency = SLAVE_LATENCY;
	ConnectParams.timeout = CONNECTION_TIMEOUT;
	GGS_SetParameter(GGS_PERI_CONN_PARAM_ATT,
			 sizeof(gapPeriConnectParams_t), &ConnectParams);

	// Setup a delayed profile startup
	tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
}
