#pragma once
/*
 * This threads runs every ms and schedules what to do in each millisecond
 * and if it is possible to send it over UART (max 90 % utilization for periodics)
 */

void periodic_thread(void *parameters);
