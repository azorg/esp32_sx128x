/*
 * MQTT wrappers
 * File: "mqtt.h"
 */

#pragma once
#ifndef MQTT_H
#define NQTT_H
//-----------------------------------------------------------------------------
#ifdef __cplusplus
#include <Adafruit_MQTT_Client.h>
extern Adafruit_MQTT_Client *Mqtt; // global variable (may be NULL)
//-----------------------------------------------------------------------------
extern "C"
{
#endif // __cplusplus
//-----------------------------------------------------------------------------
bool mqtt_connect(const char *host, uint16_t port,
                  const char *user, const char *key);
bool mqtt_connected();
void mqtt_disconnect();
//-----------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif // __cplusplus
//-----------------------------------------------------------------------------
#endif // MQTT_H

/*** end of "mqtt.h" file ***/


