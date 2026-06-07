#ifndef UTIL_H
#define UTIL_H
#include <stdint.h>
/* フレームのペイロードで運ぶデータ(8byte、リトルエンディアン) */
struct sensor_data {
	int8_t temperature;  /* 温度[°C] (1byte) */
	uint8_t humidity;  /* 湿度[%] (1byte) */
	uint16_t pressure;  /* 気圧[hPa] (2byte) */
	uint32_t timestamp;  /* 送信時刻[ms] (4byte)*/
};

#endif
