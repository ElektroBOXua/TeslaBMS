#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////
#ifndef ASYNC
typedef void * async;
#define ASYNC_CAT1(a, b) a##b
#define ASYNC_CAT(a, b) ASYNC_CAT1(a, b)
#define ASYNC_DISPATCH(state) void **_state = &state; \
			 if (*_state) { goto **_state; }
#define ASYNC_YIELD(act) do { *_state = &&ASYNC_CAT(_l, __LINE__); \
			      act; ASYNC_CAT(_l, __LINE__) :; } while (0)
#define ASYNC_AWAIT(cond, act) \
			 do { ASYNC_YIELD(); if (!(cond)) { act; } } while (0)
#define ASYNC_RESET(act) do { *_state = NULL; act; } while (0)
#define ASYNC
#endif

////////////////////////////// GENERAL DEFINITIONS ////////////////////////////
//#define TBMS_DEBUG
#define TBMS_MAX_MODULE_ADDR 0x3E
#define TBMS_MAX_COMMANDS    20
#define TBMS_MAX_IO_BUF      40

//TODO make these configurable
#define TBMS_BALANCE_VOLTAGE 3.9
#define TBMS_BALANCE_HYST    0.04

//////////////////////////// REGISTER RELATED STUFF ///////////////////////////
#define TBMS_READ       0x00
#define TBMS_WRITE      0x01
#define TBMS_BROADCAST  0x7F
#define TBMS_MODULE(n)  ((n) << 1)

#define TBMS_REG_DEV_STATUS      0
#define TBMS_REG_GPAI            1
#define TBMS_REG_VCELL1          3
#define TBMS_REG_VCELL2          5
#define TBMS_REG_VCELL3          7
#define TBMS_REG_VCELL4          9
#define TBMS_REG_VCELL5          0xB
#define TBMS_REG_VCELL6          0xD
#define TBMS_REG_TEMPERATURE1    0xF
#define TBMS_REG_TEMPERATURE2    0x11
#define TBMS_REG_ALERT_STATUS    0x20
#define TBMS_REG_FAULT_STATUS    0x21
#define TBMS_REG_COV_FAULT       0x22
#define TBMS_REG_CUV_FAULT       0x23
#define TBMS_REG_ADC_CTRL        0x30
#define TBMS_REG_IO_CTRL         0x31
#define TBMS_REG_BAL_CTRL        0x32
#define TBMS_REG_BAL_TIME        0x33
#define TBMS_REG_ADC_CONV        0x34
#define TBMS_REG_ADDR_CTRL       0x3B
#define TBMS_REG_RESET           0x3C

#define TBMS_DATA_SEL_ALL  0xFF
#define TBMS_DATA_CLR_ZRO  0x00

/////////////////////////// GLOBAL & GENERIC FUNCTIONS ////////////////////////
uint8_t tbms_gen_crc(uint8_t *data, int len)
{
	uint8_t generator = 0x07;
	uint8_t crc = 0;

	for (int j = 0; j < len; j++) {
		crc ^= data[j]; /* XOR-in the next input byte */

		for (int i = 0; i < 8; i++) {
			if ((crc & 0x80) != 0)
				crc = (uint8_t)((crc << 1) ^ generator);
			else
				crc <<= 1;
		}
	}

	return crc;
}

////////////////////// EVERYTHING RELATED TO INPUT/OUTPUT /////////////////////
enum tbms_io_state {
	TBMS_IO_STATE_IDLE,
	TBMS_IO_STATE_WAIT_FOR_SEND,
	TBMS_IO_STATE_WAIT_FOR_REPLY,
	TBMS_IO_STATE_RX_DONE,
	TBMS_IO_STATE_TIMEOUT
};

struct tbms_io {
	enum tbms_io_state state;

	async rx_state;
	async tx_state;

	bool ready;

	uint8_t buf[TBMS_MAX_IO_BUF];
	uint8_t len;
	
	clock_t timer;
	clock_t timeout;
};

void tbms_io_init(struct tbms_io *self)
{
	self->state = TBMS_IO_STATE_IDLE;
	
	self->rx_state = 0;
	self->tx_state = 0;

	self->ready = false;

	//uint8_t buf[TBMS_MAX_IO_BUF];
	self->len = 0;
	
	self->timer = 0;
	self->timeout = 100;
}

void tbms_io_reset(struct tbms_io *self)
{
	tbms_io_init(self);
}

/* Waits for module data of "expected_len" bytes
 * returns false until all conditions are met. 
 * TODO ADD CRC CHECKS. */
bool tbms_io_recv(struct tbms_io *self, uint8_t expected_len)
{
	ASYNC_DISPATCH(self->rx_state);

	self->ready = false;
	self->len   = 0;
	self->timer = 0;

	self->state = TBMS_IO_STATE_WAIT_FOR_REPLY;
	ASYNC_AWAIT((self->ready = true, self->len) >= expected_len,
		    return false);
	self->state = TBMS_IO_STATE_RX_DONE;
	ASYNC_YIELD(return false); //To track state change

	self->state = TBMS_IO_STATE_IDLE;

	ASYNC_RESET(return true);
}

/* Sends "data" of "len". Waits for module response (see tbms_io_recv)
 * returns false until all conditions are met. */
bool tbms_io_send(struct tbms_io *self, uint8_t *data, uint8_t len,
		  uint8_t expected_len)
{
	ASYNC_DISPATCH(self->tx_state);
	
	assert(len && len < TBMS_MAX_IO_BUF);
	memcpy(self->buf, data, len);
	
	//Calculate CRC for register write operation
	if (data[0] & TBMS_WRITE) {
		self->buf[0] |= 1;
		self->buf[len] = tbms_gen_crc(self->buf, len);

		len++;
	}

	self->ready = true;
	self->len = len;
	self->timer = 0;

	self->state = TBMS_IO_STATE_WAIT_FOR_SEND;
	ASYNC_AWAIT(!self->ready, return false); //Wait for user to send
	self->len = 0;

	ASYNC_AWAIT(tbms_io_recv(self, expected_len), return false);
	
	self->state = TBMS_IO_STATE_IDLE;

	ASYNC_RESET(return true);
}

/* If you want to interrupt tbms_io_send before expected_len has arrived
 * Call this command and RX will be done (as well as TX); */
bool tbms_io_rx_done(struct tbms_io *self)
{
	if (!self->rx_state)
		return true;

	// Also stop TX
	self->tx_state = 0;
	self->rx_state = 0;

	self->state = TBMS_IO_STATE_RX_DONE;
	
	return false;
}

bool tbms_io_validate_reply(struct tbms_io *self, uint8_t *reply, uint8_t len)
{
	if (!memcmp(self->buf, reply, len))
		return true;
	else
		return false;
}

void tbms_io_update(struct tbms_io *self)
{
	if (self->state == TBMS_IO_STATE_TIMEOUT)
		tbms_io_reset(self);
		
	if (!self->rx_state || !self->tx_state)
		self->timer = 0;

	if (self->timer >= self->timeout)
		self->state = TBMS_IO_STATE_TIMEOUT;
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
const char *tbms_io_get_state_name(uint8_t state)
{
	switch (state) {
	case TBMS_IO_STATE_IDLE:            return "IDLE";
	case TBMS_IO_STATE_WAIT_FOR_SEND:   return "WAIT_FOR_SEND";
	case TBMS_IO_STATE_WAIT_FOR_REPLY:  return "WAIT_FOR_REPLY";
	case TBMS_IO_STATE_RX_DONE:         return "RX_DONE";
	case TBMS_IO_STATE_TIMEOUT:         return "TIMEOUT";
	}
	
	return NULL;
}
#endif

//////////////////////////// TESLA BMS MAIN INSTANCE //////////////////////////
enum tbms_task_event { //Events returned by tasks
	TBMS_TASK_EVENT_NONE, //No event, keep running
	TBMS_TASK_EVENT_EXIT_OK,
	TBMS_TASK_EVENT_EXIT_FAULT
};

enum tbms_state {
	TBMS_STATE_INIT,
	TBMS_STATE_ESTABLISH_CONNECTION,
	TBMS_STATE_CONNECTION_ESTABLISHED
};

struct tbms_module_cell {
	float voltage;
	bool  balance; /* If cell needs to be balanced or not. */
};

struct tbms_module {
	bool exist;

	float voltage;
	
	float  temp1;
	float  temp2;

	struct tbms_module_cell cell[6];
	uint8_t balance_bits;
	
	uint8_t alerts;
	uint8_t faults;

	//Cell overvoltage and undervoltage faults
	uint8_t cov_faults;
	uint8_t cuv_faults;
};

struct tbms
{
	enum tbms_state state;

	async async_state;
	async async_task_state;

	enum tbms_task_event (**current_task)(struct tbms *self);
	
	struct tbms_io io;

	struct tbms_module modules[TBMS_MAX_MODULE_ADDR];
	uint8_t modules_count;
	uint8_t mod_sel;
	
	clock_t timer;

	bool ready;
};

void tbms_modules_init(struct tbms *self)
{
	for (int i = 0; i < TBMS_MAX_MODULE_ADDR; i++) {
		struct tbms_module *mod = &self->modules[i];

		mod->exist   = false;
		mod->voltage = 0.0;

		mod->temp1 = 0.0;
		mod->temp2 = 0.0;

		mod->balance_bits = 0;
		for (int j = 0; j < 6; j++)
			mod->cell[j].voltage = NAN;
		
		mod->alerts = 0xFF;
		mod->faults = 0xFF;

		mod->cov_faults = 0xFF;
		mod->cuv_faults = 0xFF;
	}

	self->modules_count = 0;
	self->mod_sel = 0;
}

void tbms_init(struct tbms *self)
{
	self->state = TBMS_STATE_INIT;

	self->async_state      = 0;
	self->async_task_state = 0;

	self->current_task = NULL;

	tbms_io_init(&self->io);

	tbms_modules_init(self);
	
	self->timer = 0;

	self->ready = false;
}

//////////////////// TASK DEFINITIONS ////////////////////
enum tbms_task_event tbms_task_discover(struct tbms *self)
{
	ASYNC_DISPATCH(self->async_task_state);

	uint8_t cmd[] = { TBMS_BROADCAST, TBMS_REG_RESET, 0xA5 };
			
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	uint8_t expected_reply[] = { 0x7F, 0x3C, 0xA5, 0x57 };

	if (tbms_io_validate_reply(&self->io, expected_reply, 4))
		ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);
	
	ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_FAULT);
}

enum tbms_task_event tbms_task_setup_boards(struct tbms *self)
{
	ASYNC_DISPATCH(self->async_task_state);

	uint8_t cmd[] = { TBMS_READ, TBMS_REG_DEV_STATUS, 1 };

	ASYNC_AWAIT(tbms_io_send(&self->io, cmd, 3, 3), return
		    TBMS_TASK_EVENT_NONE);

	uint8_t expected_reply[]  = { 0x80, 0x00, 0x01}; //more modules ahead
	uint8_t expected_reply2[] = { 0x00, 0x00, 0x01}; //last module
	if (!tbms_io_validate_reply(&self->io, expected_reply, 3)) {
		if (!tbms_io_validate_reply(&self->io, expected_reply2, 3))
			ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_FAULT);
		else
			ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);
	}
	
	//Skip bytes 0x61, 0x35 that appear after around 45us.
	ASYNC_AWAIT(tbms_io_recv(&self->io, 2), return TBMS_TASK_EVENT_NONE);

	int i;
	
	for (i = 0; i < TBMS_MAX_MODULE_ADDR; i++) {
		if (!self->modules[i].exist) {
			self->mod_sel = i;
			break;
		}
	}

	if (i >= TBMS_MAX_MODULE_ADDR)
		ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_FAULT);

	uint8_t cmd2[] = { TBMS_WRITE, TBMS_REG_ADDR_CTRL,
		(uint8_t)((self->mod_sel + 1) | 0x80)
	};

	ASYNC_AWAIT(tbms_io_send(&self->io, cmd2, 3, 4/*10*/) /*||
		    self->io.timer >= 50*/, return TBMS_TASK_EVENT_NONE);

	if (self->io.len < 3)
		ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_FAULT);

	uint8_t expected_reply3[] = { 
		0x81, TBMS_REG_ADDR_CTRL,
		(uint8_t)((self->mod_sel + 1) + 0x80)/*, 0x??*/};

	if (!tbms_io_validate_reply(&self->io, expected_reply3, 3))
		ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_FAULT);

	tbms_io_rx_done(&self->io);

	self->modules[self->mod_sel].exist = true;
	self->modules_count++;

	//Repeat if task not yet destroyed
	ASYNC_RESET(return TBMS_TASK_EVENT_NONE);
}

enum tbms_task_event tbms_task_clear_faults(struct tbms *self)
{
	ASYNC_DISPATCH(self->async_task_state);
	
	//Select all TBMS_REG_ALERT_STATUS status bits	
	uint8_t cmd0[] = {TBMS_BROADCAST, TBMS_REG_ALERT_STATUS,
			  TBMS_DATA_SEL_ALL };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd0, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	//Clear all TBMS_REG_ALERT_STATUS status bits
	uint8_t cmd1[] = {TBMS_BROADCAST, TBMS_REG_ALERT_STATUS,
			  TBMS_DATA_CLR_ZRO };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd1, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	//Select all TBMS_REG_FAULT_STATUS status bits	
	uint8_t cmd2[] = {TBMS_BROADCAST, TBMS_REG_FAULT_STATUS,
			  TBMS_DATA_SEL_ALL };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd2, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	//Clear all TBMS_REG_FAULT_STATUS status bits
 	uint8_t cmd3[] = {TBMS_BROADCAST, TBMS_REG_FAULT_STATUS,
			  TBMS_DATA_CLR_ZRO };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd3, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);
}

enum tbms_task_event tbms_task_read_module_status(struct tbms *self,
						  uint8_t id)
{
	struct tbms_module *mod = &self->modules[id];

	ASYNC_DISPATCH(self->async_task_state);

	uint8_t cmd0[] = {(uint8_t)(TBMS_READ | TBMS_MODULE(id + 1)),
			  TBMS_REG_ALERT_STATUS, 4 };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd0, 3, 7),
		    return TBMS_TASK_EVENT_NONE);

	mod->alerts = self->io.buf[3];
	mod->faults = self->io.buf[4];

	//Cell overvoltage and undervoltage faults
	mod->cov_faults = self->io.buf[5];
	mod->cuv_faults = self->io.buf[6];
	
	ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);
}

enum tbms_task_event tbms_task_read_module_values(struct tbms *self, uint8_t id)
{
	struct tbms_module *mod = &self->modules[id];

	ASYNC_DISPATCH(self->async_task_state);
	
	//ADC Auto mode, read every ADC input we can(Both Temps, Pack, 6 cells)
	uint8_t cmd0[] = {(uint8_t)(TBMS_WRITE | TBMS_MODULE(id + 1)),
			  TBMS_REG_ADC_CTRL, 0b00111101 };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd0, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	//enable temperature measurement VSS pins
  	uint8_t cmd1[] = {(uint8_t)(TBMS_WRITE | TBMS_MODULE(id + 1)),
			  TBMS_REG_IO_CTRL, 0b00000011 };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd1, 3, 4),
		    return TBMS_TASK_EVENT_NONE);


	//start all ADC conversions
  	uint8_t cmd2[] = {(uint8_t)(TBMS_WRITE | TBMS_MODULE(id + 1)),
			  TBMS_REG_ADC_CONV, 1 };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd2, 3, 4),
		    return TBMS_TASK_EVENT_NONE);


	//start reading registers at the module voltage registers
  	//read 18 bytes (Each value takes 2 - ModuleV, CellV1-6, Temp1, Temp2)
  	uint8_t cmd3[] = {(uint8_t)(TBMS_READ | TBMS_MODULE(id + 1)),
			  TBMS_REG_GPAI, 0x12 };
	ASYNC_AWAIT(tbms_io_send(&self->io, cmd3, 3, 22),
		    return TBMS_TASK_EVENT_NONE);


	int crc = tbms_gen_crc(self->io.buf, self->io.len - 1);
        
	uint8_t *buf = self->io.buf;

	//18 data bytes, address, command, length, and CRC = 22 bytes returned
	//Also validate CRC to ensure we didn't get garbage data.
	//Also ensure this is actually the reply to our intended query
	if (buf[21] == crc && buf[0] == TBMS_MODULE(id + 1) &&
	    buf[1] == TBMS_REG_GPAI && buf[2] == 18) {
		//printf("CRC MATCH, EVERYTHING OK\n");
		mod->voltage = (buf[3] * 256 + buf[4]) * 0.002034609f;
		
		for (int i = 0; i < 6; i++)
			mod->cell[i].voltage = 
				(buf[5 + (i * 2)] * 256 + buf[6 + (i * 2)]) *
				 0.000381493f;

		float temp;
		float temp_calc;

		temp = (1.78f / ((buf[17] * 256 + buf[18] + 2) /
			    33046.0f) - 3.57f) * 1000.0f;
		temp_calc =  1.0f / (0.0007610373573f + 
			    (0.0002728524832 * logf(temp)) +
			    (powf(logf(temp), 3) * 0.0000001022822735f));

		mod->temp1 = temp_calc - 273.15f;     

		temp = (1.78f / ((buf[19] * 256 + buf[20] + 2) /
			    33046.0f) - 3.57f) * 1000.0f;
		temp_calc =  1.0f / (0.0007610373573f + 
			    (0.0002728524832 * logf(temp)) +
			    (powf(logf(temp), 3) * 0.0000001022822735f));

		mod->temp2 = temp_calc - 273.15f;

	} else {
		//printf("CRC MISMATCH, EVERYTHING IS BAD\n");
	}
	
	ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);
}

enum tbms_task_event tbms_task_balance_cells(struct tbms *self, uint8_t id)
{
	struct tbms_module *mod = &self->modules[id];

	ASYNC_DISPATCH(self->async_task_state);

	mod->balance_bits = 0; //bit 0-5 are to activate cell balancing 1-6

	for (int i = 0; i < 6; i++) {
		//If voltage greater than activation threshold - set balance on
		if (mod->cell[i].voltage > TBMS_BALANCE_VOLTAGE)
			mod->cell[i].balance = true;

		//If voltage is lower than release threshold - set balance off
		if (mod->cell[i].voltage <
		   (TBMS_BALANCE_VOLTAGE - TBMS_BALANCE_HYST))
			mod->cell[i].balance = false;

		mod->balance_bits |= ((mod->cell[i].balance ? 1 : 0) << i);
	}
	
	if (!mod->balance_bits)
		ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);

	uint8_t cmd0[] = {(uint8_t)(TBMS_WRITE | TBMS_MODULE(id + 1)),
			  TBMS_REG_BAL_CTRL, 0 };
	/* last byte resets balance time and must be done
	   before setting balance resistors again. */

	ASYNC_AWAIT(tbms_io_send(&self->io, cmd0, 3, 4),
		    return TBMS_TASK_EVENT_NONE);

	if (mod->balance_bits) //only send balance command when needed
	{
		uint8_t cmd0[] = {(uint8_t)(TBMS_WRITE | TBMS_MODULE(id + 1)),
				  TBMS_REG_BAL_TIME, 130 };
		//last byte sets balance for 130 seconds

		ASYNC_AWAIT(tbms_io_send(&self->io, cmd0, 3, 4),
			    return TBMS_TASK_EVENT_NONE);


		uint8_t cmd1[] = {(uint8_t)(TBMS_WRITE | TBMS_MODULE(id + 1)),
				  TBMS_REG_BAL_CTRL, mod->balance_bits };
		//write balance state to register

		ASYNC_AWAIT(tbms_io_send(&self->io, cmd1, 3, 4),
			    return TBMS_TASK_EVENT_NONE);
	}
	
	ASYNC_RESET(return TBMS_TASK_EVENT_EXIT_OK);
}

//////////////////// API ////////////////////
bool tbms_tx_available(struct tbms *self)
{
	if (self->io.state == TBMS_IO_STATE_WAIT_FOR_SEND && self->io.ready)
		return true;
	
	return false;
}

bool tbms_rx_available(struct tbms *self)
{
	if (self->io.state == TBMS_IO_STATE_WAIT_FOR_REPLY && self->io.ready)
		return true;
	
	return false;
}

void tbms_set_rx(struct tbms *self, uint8_t byte)
{
	assert(tbms_rx_available(self));
	self->io.ready = false;

	assert(self->io.len < TBMS_MAX_IO_BUF);
	self->io.buf[self->io.len++] = byte;
}

size_t tbms_get_tx_len(struct tbms *self)
{
	return self->io.len;
}

uint8_t *tbms_get_tx_buf(struct tbms *self)
{
	return self->io.buf;
}

void tbms_tx_flush(struct tbms *self)
{
	if (self->io.state == TBMS_IO_STATE_WAIT_FOR_SEND)
		self->io.ready = false;
}

bool tbms_has_faults(struct tbms *self)
{
	for (int i = 0; i < TBMS_MAX_MODULE_ADDR; i++) {
		struct tbms_module *mod = &self->modules[i];
		
		if (!mod->exist)
			continue;
		
		if (/*mod->alerts || */mod->faults || mod->cov_faults || 
		    mod->cuv_faults)
			return true;
	}

	return false;
}

//Returns true if TBMS is safe to use
bool tbms_is_ready(struct tbms *self)
{
	return self->ready && !tbms_has_faults(self);
}

//////////////////// API (MODULE) ////////////////////
#define TBMS_MODULE_METHOD_CHECKS(ret) \
	if (id >= TBMS_MAX_MODULE_ADDR) id = TBMS_MAX_MODULE_ADDR - 1; \
	if (!self->modules[id].exist) \
		return ret//-273.15f;

float tbms_get_module_temp1(struct tbms *self, uint8_t id)
{
	TBMS_MODULE_METHOD_CHECKS(NAN);//-273.15f;

	return self->modules[id].temp1;
}

float tbms_get_module_temp2(struct tbms *self, uint8_t id)
{
	TBMS_MODULE_METHOD_CHECKS(NAN);//-273.15f;

	return self->modules[id].temp2;
}

float tbms_get_module_voltage(struct tbms *self, uint8_t id)
{
	TBMS_MODULE_METHOD_CHECKS(NAN);
	
	return self->modules[id].voltage;
}

float tbms_get_module_cell_voltage(struct tbms* self, uint8_t id, uint8_t cn)
{
	TBMS_MODULE_METHOD_CHECKS(NAN);
	
	return self->modules[id].cell[cn].voltage;
}

//////////////////// UPDATE ////////////////////
void tbms_update(struct tbms *self, clock_t delta)
{
	enum tbms_task_event event;

	//Update theese in any case
	self->io.timer += delta;
	self->timer    += delta;

	tbms_io_update(&self->io);

	//If there is any INPUT/OUTPUT timeout
	if (self->io.state == TBMS_IO_STATE_TIMEOUT) {
		//Reset any running task
		self->async_task_state = 0;
		
		//Goto reset state (initial)
		self->state = TBMS_STATE_INIT;
		self->async_state = 0;

		self->ready = false;
	}

	ASYNC_DISPATCH(self->async_state);

	//Tasks to perform to establish connection
	static enum tbms_task_event (*task_list[])(struct tbms *self) = {
		tbms_task_discover, tbms_task_setup_boards,
		tbms_task_clear_faults, NULL //terminator
	};

	switch (self->state) {
	case TBMS_STATE_INIT:
		self->ready = false;
	
		//Wait 1 second before initialization
		self->timer = 0;
		ASYNC_AWAIT(self->timer >= 1000, return);

		//Reset all modules state
		tbms_modules_init(self);

		self->current_task = &task_list[0];
		self->state = TBMS_STATE_ESTABLISH_CONNECTION;
		break;

	//This routine explains how connection is performed
	case TBMS_STATE_ESTABLISH_CONNECTION:
		//If all tasks were done
		if (*self->current_task == NULL) {
			self->state = TBMS_STATE_CONNECTION_ESTABLISHED;
			break;
		}

		event = (*self->current_task)(self);
		
		if (event == TBMS_TASK_EVENT_NONE)
			break;

		if (event == TBMS_TASK_EVENT_EXIT_OK) {
			self->current_task++;
			break;
		}
			
		//Something went wrong, repeat after 1s
		self->timer = 0;
		ASYNC_AWAIT(self->timer >= 1000, return);

		break;

	case TBMS_STATE_CONNECTION_ESTABLISHED:
		//We can not proceed with zero modules
		if (self->modules_count <= 0) {
			self->state = TBMS_STATE_INIT;
			self->async_state = 0;
			break;
		}

		//Iterate through all modules
		for (self->mod_sel = 0; self->mod_sel < TBMS_MAX_MODULE_ADDR;
		     self->mod_sel++) {
			if (!self->modules[self->mod_sel].exist)
				continue;

			//Read module values
			ASYNC_AWAIT(
				tbms_task_read_module_values(self,
							     self->mod_sel) !=
				TBMS_TASK_EVENT_NONE, return);

			//Balance cells
			ASYNC_AWAIT(
				tbms_task_balance_cells(self, self->mod_sel) !=
				TBMS_TASK_EVENT_NONE, return);

			//Read module status
			ASYNC_AWAIT(
				tbms_task_read_module_status(self,
							     self->mod_sel) !=
				TBMS_TASK_EVENT_NONE, return);
		}

		self->ready = true;
		
		self->timer = 0;
		ASYNC_AWAIT(self->timer >= 1000, return);
		
		break;
	}
	
	ASYNC_RESET(return);
}

//////////////////// DEBUG ////////////////////
#ifdef   TBMS_DEBUG
struct tbms_debug
{
	struct tbms tb;
	
	uint8_t io_flags;
	uint8_t io_state;
	
	clock_t timestamp;
};

void tbms_init_debug(struct tbms_debug *self)
{
	self->io_state = 0;
	self->io_flags = 0;

	self->timestamp = 0;

	tbms_init(&self->tb);
}

void tbms_update_debug(struct tbms_debug *self, clock_t delta)
{
	tbms_update(&self->tb, delta);

	if (self->io_state != self->tb.io.state) {
		printf("[% 3i] tbms.io.state: %s -> %s\n",
			(int)self->timestamp,
		//printf("tbms.io.state: %s -> %s\n",
			tbms_io_get_state_name(self->io_state),
			tbms_io_get_state_name(self->tb.io.state));
		self->io_state = self->tb.io.state;	
	}
	
	if (tbms_tx_available(&self->tb)) {
		printf("[% 3i] tbms.io.txbuf: ", (int)self->timestamp);
		//printf("tbms.io.txbuf: ");

		for (size_t i = 0; i < tbms_get_tx_len(&self->tb); i++)
			printf("0x%02X ", tbms_get_tx_buf(&self->tb)[i]);

		printf("\n");
	}
	
	if (self->io_state == TBMS_IO_STATE_RX_DONE ||
	    self->io_state == TBMS_IO_STATE_TIMEOUT) {
		printf("[% 3i] tbms.io.rxbuf: ", (int)self->timestamp);
		//printf("tbms.io.rxbuf: ");

		for (size_t i = 0; i < self->tb.io.len; i++)
			printf("0x%02X ", self->tb.io.buf[i]);

		printf("\n");
	}
	
	self->timestamp += delta;
}

typedef struct tbms tbms_orig;
#define tbms_tx_available(s) tbms_tx_available((tbms_orig *)s)
#define tbms_get_tx_len(s)   tbms_get_tx_len((tbms_orig *)s)
#define tbms_get_tx_buf(s)   tbms_get_tx_buf((tbms_orig *)s)
#define tbms_tx_flush(s)     tbms_tx_flush((tbms_orig *)s)
#define tbms_rx_available(s) tbms_rx_available((tbms_orig *)s)
#define tbms_set_rx(s, a)    tbms_set_rx((tbms_orig *)s, a)
#define tbms_is_ready(s)     tbms_is_ready((tbms_orig *)s)
#define tbms_get_module_temp1(s, a) \
	tbms_get_module_temp1((tbms_orig *)s, a)
#define tbms_get_module_voltage(s, a) \
	tbms_get_module_voltage((tbms_orig *)s, a)
#define tbms_get_module_cell_voltage(s, a, b) \
	tbms_get_module_cell_voltage((tbms_orig *)s, a, b)
	
#define tbms        tbms_debug
#define tbms_init   tbms_init_debug
#define tbms_update tbms_update_debug

#endif //TBMS_DEBUG
