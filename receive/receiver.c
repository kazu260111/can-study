/*
 * 生の仮想CANフレームを受信するプログラム
 *
 * 更新履歴:
 * 第4回 フィルタを設定するコードを追加、複数フレームをループで受信するがctrl+cで強制終了する必要がある
 * 
 * ソケットを開けてから受信までの処理はほぼ学習メモ第1回の内容通り。
 * それに加えて読んだフレームを独自の構造体にキャストして意味のあるデータとして
 * 解釈できるようにする。今回は単純化のため一つのフレームで一つのデータとして
 * 成立するようにする。(8バイトで収まるデータにして、複数フレームからの組み立ては不要にする)
 * データのバイトオーダーはすべてリトルエンディアンで統一する。
 */

#include <linux/can.h>
#include <linux/can/raw.h> 
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
	/*>>> ソケットを開けてから受信までの処理 <<<*/
	/* ソケットをオープンする */
	fprintf(stderr, "[D] 受信プログラムの開始、socket()を実行\n");
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
	/* フィルタの設定 */
	struct can_filter rfilter;
	rfilter.can_id = 0x123;
	rfilter.can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;	/* 標準フレームのみ通すようにに設定 */
	/* フィルタ設定の実行 */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter)) == -1) {
		fprintf(stderr, "setsockopt()に失敗: %s\n", strerror(errno));
		close(s);
		return 1;
	}
		
	/*>>> 受信(エラーにならない限りループ) <<<*/
	while (1) {
		/* read()の実行 */
		struct can_frame frame;
		ssize_t num_read = read(s, &frame, sizeof(struct can_frame));
		/* read()で読み取れなかったとき */
		if (num_read < 0) {
			fprintf(stderr, "[E] read()でフレームを読み取れませんでした: %s\n", strerror(errno));
			close(s);
			return 1;
		}
		/* 規定の16byteを読み取れないとき */
		if (num_read < (ssize_t)sizeof(struct can_frame)) {
		       fprintf(stderr, "[E] 読み取れたフレームが規定(16バイト)より少ないです \n");
		       close(s);
		       return 1;
	 	}
		fprintf(stderr, "[D] フレームの読み取りが完了しました\n");	
	
		/*>>> フレームの解釈とデータ表示 <<<*/
		fprintf(stderr, "[D] フレームの解釈を開始します\n");
		struct sensor_data data;
		/* フレームのペイロードをsensor_data型の変数に入れる */
		memcpy(&data, frame.data, sizeof(data));
		fprintf(stderr, "[D] 受け取ったデータを表示します\n");
		/* 受け取ったデータの表示開始 */
		printf("温度: %d °C\n"
		       "湿度: %u %%\n"
		       "気圧: %u hPa\n"
		       "送信時刻: %u ms\n",
		       data.temperature,
		       data.humidity,
		       data.pressure,
		       data.timestamp);
	}

	/*>>> 終了処理(到達しない) <<<*/
	fprintf(stderr, "[D] ソケットを閉じて終了します\n");
	close(s);
	return 0;
}
