#include "FreeRTOS.h"

#include "task.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <zenoh-pico/system/platform_common.h>

#include "autoconf.h"
#include "param.h"
#include "system.h"
#include "timers.h"

#include "zenoh-pico.h"

#undef DEBUG_MODULE
#define DEBUG_MODULE "ZENOH"
#include "debug.h"

#include "log.h"
#include "uart1.h"
#include "uart2.h"

static uint16_t zenohSent;

#define KEYEXPR "demo/example/pos"

static void zenohTask(void *args) {
  DEBUG_PRINT("init: %d\n", xPortGetFreeHeapSize());

  z_owned_config_t config;
  z_config_default(&config);
  zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY,
                   "serial/usart#baudrate=38400");

  DEBUG_PRINT("Opening session...: %d\n", xPortGetFreeHeapSize());

  z_owned_session_t session;
  if (z_open(&session, z_move(config), NULL) < 0) {
    DEBUG_PRINT("Unable to open session!\n");
    return;
  }

  static StackType_t read_task_stack[500];
  static StaticTask_t read_task_buffer;

  z_task_attr_t read_task_attr = {
      .name = "ZenohReadTask",
      .priority = 10,
      .stack_depth = 500,
      .static_allocation = true,
      .stack_buffer = read_task_stack,
      .task_buffer = &read_task_buffer,
  };

  zp_task_read_options_t read_task_opt = {.task_attributes = &read_task_attr};

  static StackType_t lease_task_stack[500];
  static StaticTask_t lease_task_buffer;

  z_task_attr_t lease_task_attr = {
      .name = "ZenohLeaseTask",
      .priority = 10,
      .stack_depth = 500,
      .static_allocation = true,
      .stack_buffer = lease_task_stack,
      .task_buffer = &lease_task_buffer,
  };

  zp_task_lease_options_t lease_task_opt = {.task_attributes =
                                                &lease_task_attr};

  if (zp_start_read_task(z_loan_mut(session), &read_task_opt) < 0 ||
      zp_start_lease_task(z_loan_mut(session), &lease_task_opt) < 0) {
    DEBUG_PRINT("Unable to start read and lease tasks!\n");
    z_drop(z_move(session));
    return;
  }

  DEBUG_PRINT("Declaring publisher...: %d\n", xPortGetFreeHeapSize());
  z_view_keyexpr_t ke;
  z_view_keyexpr_from_str_unchecked(&ke, KEYEXPR);
  z_owned_publisher_t pub;
  if (z_declare_publisher(z_loan(session), &pub, z_loan(ke), NULL) < 0) {
    DEBUG_PRINT("Unable to declare publisher\n");
    z_drop(z_move(session));
    return;
  }

  DEBUG_PRINT("Start loop: %d\n", xPortGetFreeHeapSize());

  int pitchid = logGetVarId("stabilizer", "pitch");
  int rollid = logGetVarId("stabilizer", "roll");

  char buf[100];
  z_owned_bytes_t payload;
  while (true) {
    float roll = logGetFloat(rollid);
    float pitch = logGetFloat(pitchid);

    snprintf(buf, 100, "%i: %i,%i", ++zenohSent, (int)pitch, (int)roll);
    z_bytes_copy_from_str(&payload, buf);
    if (z_publisher_put(z_loan(pub), z_move(payload), NULL)) {
      DEBUG_PRINT("Unable to send message\n");
      z_drop(z_move(session));
      return;
    }
    z_sleep_ms(300);
  }
}

void zenohInit(void) {
  TaskHandle_t xHandle = NULL;
  BaseType_t xReturned =
      xTaskCreate(zenohTask, "zenoh", 500, NULL, tskIDLE_PRIORITY, &xHandle);
  if (xReturned != pdPASS) {
    DEBUG_PRINT("xTaskCreate failed\n");
    return;
  }

  DEBUG_PRINT("init done\n");
}

LOG_GROUP_START(zenoh)
LOG_ADD_CORE(LOG_UINT16, sent, &zenohSent)
LOG_GROUP_STOP(range)

// Stub methods to avoid linker errors
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int _write(int file, char *ptr, int len) { return len; }

int _read(int file, char *ptr, int len) {
  return 0; // EOF
}

caddr_t _sbrk(int incr) {
  extern char _end;
  static char *heap_end;
  char *prev_heap_end;

  if (heap_end == 0) {
    heap_end = &_end;
  }
  prev_heap_end = heap_end;

  heap_end += incr;
  return (caddr_t)prev_heap_end;
}

int _close(int file) { return -1; }

int _fstat(int file, struct stat *st) {
  st->st_mode = S_IFCHR;
  return 0;
}

int _lseek(int file, int ptr, int dir) { return 0; }

int _isatty(int file) { return 1; }
