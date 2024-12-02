#include <stdarg.h>
#include "CH58xBLE_LIB.h"
#include "CONFIG.h"
#include "fifo8.h"
#include "stepforth.h"
#include "ble.h"

enum {
	CONNECTION_INTERVAL_MIN = 9, // x 1.25ms =  11.25ms
	CONNECTION_INTERVAL_MAX = 100, // x 1.25ms = 125ms
	CONNECTION_TIMEOUT = 100, // x 0.1ms = 10ms
	SLAVE_LATENCY = 0,
	ADVERTISING_INTERVAL_MIN = 80, // x 0.625ms = 50ms
	ADVERTISING_INTERVAL_MAX = 160, // x 0.625ms = 100ms
	PARAM_UPDATE_DELAY = 6400, // x 0.625ms
	PHY_UPDATE_DELAY = 3200, // x 0.625ms
	PERIOD_READ_RSSI = 3200, // x 0.625ms
	FORTH_DELAY = 2,
};

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
extern uint8_t chip_uid[8];
extern uint16_t chip_uid_sum;

struct ble_peri_slot ble_peri_slots[PERIPHERAL_MAX_CONNECTION];

enum {
	STATE_DEV_CONNECTED = (1 << 0),
};

int ble_peri_slots_find_free(void)
{
	int slotp;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].state == 0) {
			return slotp;
		}
	}
	return -1;
}

int ble_peri_slots_is_full(void)
{
	return (ble_peri_slots_find_free() < 0);
}

int ble_peri_slots_used(void)
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

int ble_peri_slots_free(void)
{
	return (PERIPHERAL_MAX_CONNECTION - ble_peri_slots_used());
}

int ble_peri_slots_find_by_connHandle(int connHandle)
{
	int slotp;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].connHandle == connHandle) {
			return slotp;
		}
	}
	return -1;
}

int ble_peri_slots_find_by_taskID(int task_id)
{
	int slotp;
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		if (ble_peri_slots[slotp].taskID == task_id) {
			return slotp;
		}
	}
	return -1;
}

static uint8_t Peripheral_TaskID = INVALID_TASK_ID;

// Peripheral Task Events
enum {
	SBP_START_DEVICE_EVT = (1 << 0),
	SBP_PERIODIC_EVT = (1 << 1),
	SBP_READ_RSSI_EVT = (1 << 2),
	SBP_PARAM_UPDATE_EVT = (1 << 3),
	SBP_PHY_UPDATE_EVT = (1 << 4),
	SBP_FORTH_EVT = (1 << 5),
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

static void Peripheral_LinkTerminated(gapRoleEvent_t *pEvent)
{
	PERI_DBG_PRINT("Disconnected...Reason: 0x%02X\n\r",
		       pEvent->linkTerminate.reason);
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(
		pEvent->linkTerminate.connectionHandle);
	if (slotp < 0) {
		PERI_PANIC();
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
	ble_peri_slots[slotp].periodic_delay =
		(connInterval + (connInterval >> 1));
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

extern void peripheralSysInfoSysClockNotify(uint16_t connHandle);
extern void peripheralConsoleRNWNotify(uint16_t connHandle);

static void performPeriodicTask(uint16_t connHandle)
{
	int slotp;
	slotp = ble_peri_slots_find_by_connHandle(connHandle);
	ble_peri_slots[slotp].periodic_cnt++;

	peripheralSysInfoSysClockNotify(connHandle);
	peripheralConsoleRNWNotify(connHandle);
}

static uint16_t peri_connect_ProcessEvent(uint8_t task_id, uint16_t events)
{
	int slotp;
	slotp = ble_peri_slots_find_by_taskID(task_id);
	if (slotp < 0) {
		PERI_PANIC();
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

	if (events & SBP_FORTH_EVT) {
		// Restart timer
		if (FORTH_DELAY) {
			tmos_start_task(ble_peri_slots[slotp].taskID,
					SBP_FORTH_EVT, FORTH_DELAY);
		}
		int cnt = 100;
		do {
			stepforth(NULL);
		} while (cnt--);
		return (events ^ SBP_FORTH_EVT);
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

extern bStatus_t GATT_AddSysInfo_Service(void);
extern bStatus_t GATT_AddConsole_Service(void);

void Peripheral_Init(void)
{
	PERI_DBG_PRINT("BLE Peripheral Init...\n\r");
	snprintf(attDeviceName, GAP_DEVICE_NAME_LEN - 1, "CH5%02X-%04X",
		 R8_CHIP_ID, chip_uid_sum);
	memcpy(&scanRspData[2], attDeviceName, MIN(10, strlen(attDeviceName)));
	PERI_DBG_PRINT("Device Name: %s\n\r", attDeviceName);

	Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

	int slotp;
	memset(ble_peri_slots, 0x0, sizeof(ble_peri_slots));
	for (slotp = 0; slotp < PERIPHERAL_MAX_CONNECTION; slotp++) {
		ble_peri_slots[slotp].taskID =
			TMOS_ProcessEventRegister(Peripheral_ProcessEvent);
		ble_peri_slots[slotp].connHandle = GAP_CONNHANDLE_INIT;

		ble_peri_slots[slotp].sfm.task_addr =
			&ble_peri_slots[slotp].sft;
		ble_peri_slots[slotp].sfm.ble_peri_slot_addr =
			&ble_peri_slots[slotp];

		ble_peri_slots[slotp].conrx_fifo.buf =
			ble_peri_slots[slotp].conrx_fifo_buf;
		ble_peri_slots[slotp].conrx_fifo.size = CONFIFO_SIZE;
		ble_peri_slots[slotp].contx_fifo.buf =
			ble_peri_slots[slotp].contx_fifo_buf;
		ble_peri_slots[slotp].contx_fifo.size = CONFIFO_SIZE;
		fifo8_reset(&ble_peri_slots[slotp].conrx_fifo);
		fifo8_reset(&ble_peri_slots[slotp].contx_fifo);
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
	GATT_AddSysInfo_Service();
	GATT_AddConsole_Service();

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
