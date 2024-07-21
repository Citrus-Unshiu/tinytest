#ifndef __CONFIG_H__
#define __CONFIG_H__
#include "logger.h"

// IN_CSE, MN_CSE
#define SERVER_TYPE MN_CSE 

// #define NIC_NAME "eth0"
#define SERVER_IP "192.168.142.139"
#define SERVER_PORT "3000"
#define CSE_BASE_NAME "TinyIoT1"
#define CSE_BASE_RI "tinyiot1"

#if SERVER_TYPE == MN_CSE
#define REMOTE_CSE_ID "5-20240602142523343966"
#define REMOTE_CSE_NAME "Mobius"
#define REMOTE_CSE_HOST "192.168.57.128"
#define REMOTE_CSE_PORT 7579
#endif

// #if SERVER_TYPE == MN_CSE
// #define REMOTE_CSE_ID "tinyiot"
// #define REMOTE_CSE_NAME "TinyIoT"
// #define REMOTE_CSE_HOST "192.168.142.138"
// #define REMOTE_CSE_PORT 3001
// #endif

#define MONO_THREAD 0 // 0 → multi-thread, 1 → mono-thread

#define MAX_PAYLOAD_SIZE 65536 
#define MAX_URI_SIZE 1024
#define MAX_ATTRIBUTE_SIZE 4096

#define MAX_TREE_VIEWER_SIZE 65536
#define DEFAULT_EXPIRE_TIME -3600*24*365*2

#define SOCKET_TIMEOUT 3 // seconds
#define MAX_URI_LENGTH 1024

// AE Settings
// #define ALLOW_AE_ORIGIN "C*,S*" , no blankspace allowed
#define ALLOW_AE_ORIGIN "C*,S*,/*"

// CNT Settings
#define DEFAULT_MAX_NR_INSTANCES 10
#define DEFAULT_MAX_BYTE_SIZE 65536

// CIN Settings
#define ALLOW_CIN_RN true // true → allow, false → not allow

// Group Settings
#define DEFAULT_CONSISTENCY_POLICY CSY_ABANDON_MEMBER


// Discovery Settings
#define DEFAULT_DISCOVERY_LIMIT 150


// MQTT Settings
// #define ENABLE_MQTT

#ifdef ENABLE_MQTT
#define MQTT_HOST            "127.0.0.1"
#define MQTT_QOS             MQTT_QOS_0
#define MQTT_KEEP_ALIVE_SEC  60
#define MQTT_CMD_TIMEOUT_MS  30000
#define MQTT_CON_TIMEOUT_MS  5000
#define MQTT_CLIENT_ID       "TinyIoT"
#define MQTT_USERNAME        "test"
#define MQTT_PASSWORD        "mqtt"

#define topicPrefix ""

// MQTT TLS Setting
#ifdef ENABLE_MQTT_TLS
    #define MQTT_USE_TLS     1
    #define MQTT_PORT        8883
#else
    #define MQTT_USE_TLS     0
    #define MQTT_PORT        1883
#endif

#define MQTT_MAX_PACKET_SZ   16384
#define INVALID_SOCKET_FD    -1
#define PRINT_BUFFER_SIZE    16384

#endif

#define LOG_LEVEL LOG_LEVEL_DEBUG
#define LOG_BUFFER_SIZE MAX_PAYLOAD_SIZE

#endif

// // CoAP Settings
// #define ENABLE_COAP
// #ifdef ENABLE_COAP
// #define COAP_PORT 5683

// // #define ENABLE_COAP_DTLS
// #ifdef ENABLE_COAP_DTLS
//     #define COAP_PORT 5684
// #endif
// #endif
