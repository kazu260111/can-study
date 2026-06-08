/*
 * 生の仮想CANフレームを送信するプログラム
 * 
 * ソケットを開けてからbind()までの処理は受信側と同じ。
 * その後送信用のフレームを組み立ててwrite()で送信する。
 * データのバイトオーダーはすべてリトルエンディアンで統一する。
 */

#include <linux/can.h>
#include <linux/sockios.h>
#include <stdio.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>
#include "../util.h"
#include <stdint.h>

int main() {
	/*>>> ソケットを開けてからbind()までの処理 <<<*/
	/* ソケットをオープンする */
	fprintf(stderr, "[D] 送信プログラムの開始、socket()を実行\n");
	int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s == -1) {
		fprintf(stderr, "[E] socket()失敗: %s\n", strerror(errno));
		return 1;
	}
	/* インターフェースのインデックス番号を調べる準備 */
	struct ifreq ifr;
	strcpy(ifr.ifr_name, "vcan0");
	/* インデックス番号を解決し、ifr.ifr_indexに調べた番号を入れる */
	if (ioctl(s, SIOCGIFINDEX, &ifr) == -1) {
		fprintf(stderr, "[E] ioctl()でインデックス番号の解決に失敗: %s\n", strerror(errno));
		close(s);
		return 1;
	}
	
	/* sockaddr_canの設定 */
	struct sockaddr_can addr;
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	/* bind()の実行 */
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
		fprintf(stderr, "[E] bind()失敗: %s\n", strerror(errno));
		close(s);
		return 1;
	}

	/*>>> フレームの組み立て <<<*/ 
	/* 0で初期化(使わない領域を確実に0で埋めておく) */
	struct can_frame frame = {0};
	/* can_idの設定 */
	frame.can_id = 0x123;
	/* データの長さの設定 */
	frame.len = sizeof(struct sensor_data);
	/* ペイロードに入れるデータの設定 */
	/* ホストがリトルエンディアンなのでこのまま設定してよい */
	struct sensor_data data;
	data.temperature = 25;  /* 1byte */
	data.humidity = 50;  /* 1byte */
	data.pressure = 1013;  /* 2byte */
	data.timestamp = 0x1234ABCD;  /* 4byte */
	/* frameにデータを入れる */
	/* リトルエンディアンで統一されていて、構造体にパディングもないのでこのままmemcpy()でよい */
	memcpy(frame.data, &data, sizeof(data));

	/*>>> フレームの送信 <<<*/
	ssize_t num_write = write(s, &frame, sizeof(struct can_frame));
	if (num_write == -1) {
		fprintf(stderr, "[E] write()に失敗しました: %s\n", strerror(errno));
		close(s);
		return 1;
	}
	if (num_write < (ssize_t)sizeof(struct can_frame)) {
		fprintf(stderr, "[E] 送信したフレームのバイト数が規定より少ないです: %zdバイト\n", num_write);
		close(s);
		return 1;
	}
	/*>>> 終了処理 <<<*/
	fprintf(stderr, "[D] ソケットを閉じて終了します\n");
	close(s);
	return 0;
}

