# 目的
- 車載ネットワークとして使われるCANについて自分でプログラムを作成しながら理解を深める

# 参考書籍
- **Linuxプログラミングインターフェース**
  - 著者: Michael Kerrisk
  - 訳者: 千住　治郎
  - 出版社: オライリー・ジャパン
  - 本文中ではTLPIと表記


# 参考資料
[SocketCAN - Controller Area Network(Linuxカーネル)](https://docs.kernel.org/networking/can.html)

# 学習メモ
## 第4回 フィルタリングの実装
### 学習内容
[SocketCAN - Controller Area Network(Linuxカーネル)](https://docs.kernel.org/networking/can.html)の"RAW Protocol Sockets with can_filters (SOCK_RAW)"から引用・参考にした。
- 生のパケットを受信するときにフィルタリング機能を追加してcan_idの異なるフレームは受け取らないようにする

#### フィルタリングの仕組み
RAWパケットを受信するときはデフォルトでいくつかの設定がされる。
- すべてのパケットを受信する
- エラーメッセージは受け取らない
- ループバックは有効だが、他のソケットが受け取れる(自身は受け取れない)

今回は設定を変更してパケットをフィルタリングして特定のcan_idのものだけ受信できるようにする。
フィルターを設定するためには以下の構造体を使う。

```c
struct can_filter {
    canid_t can_id;
    canid_t can_mask;
};
```
can_idとcan_maskのビット論理積によってパケットを受け入れるかが決まる。
式で表すと以下を満たせば良い。
```text
([受信フレームのcan_id] & can_mask) == (can_id & can_mask)
```
言い換えると、can_maskは受信フレームのcan_idに対してフィルタ側のcan_idのビットがどれだけ一致していればよいかを示している。

can_maskについて理解するために定義されたmaskとcanid_tの仕様を確認する。(linux/can.h)
```c
/* special address description flags for the CAN_ID */
#define CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U /* error message frame */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */
#define CANXL_PRIO_MASK CAN_SFF_MASK /* 11 bit priority mask */

/*
 * Controller Area Network Identifier structure
 *
 * bit 0-28	: CAN identifier (11/29 bit)
 * bit 29	: error message frame flag (0 = data frame, 1 = error message)
 * bit 30	: remote transmission request flag (1 = rtr frame)
 * bit 31	: frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
 */
typedef __u32 canid_t;
```
ヘッダファイルから以下のことがわかる。
- 上位ビットはフレームフォーマットフラグ(エラーメッセージ、リモート、標準または拡張のフラグ)
- 下位11bit(拡張なら29bit)がcan id用に設定されている

今回標準フォーマットでcan idが一致すれば受信すると決めた場合、maskは下位11bitと上位1bitに1を設定すればよい。
また、リモートフレームの処理は実装してないので受け取らないようにbit 30(下位から数えて31bit目)も判定する必要があるので以下のようにmaskを設定する。
```text
mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG;
```
エラーメッセージのフラグは設定不要(デフォルトで受け取らないので設定しないと来ない)

今回はフィルターは一つで実験するが、複数設定することも可能(どれかが条件を満たせば受信できる)。
例えば2つフィルターを設定するときは以下のように定義する。
```c
struct can_filter rfilter[2];
```

フィルタを実際に設定するときは以下のようにする(bind()の前後で可能)。
```c
/* CAN_RAW_FILTERに <linux/can/raw.h> が必要 */
setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter)); 
```

#### 受信プログラムの変更
既存のreceiver.cを変更してフィルタを設定する。
```c
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
```

#### 送信プログラムの変更
既存のsend.cを変更して、複数のcan_idのフレームを送るようにする。
```c
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
#define ERROR_WRITE 2
#define ERROR_BYTE_COUNT 3

void error_print(int err_num, ssize_t num_write) {
	if (err_num == ERROR_WRITE) {
		fprintf(stderr, "[E] write()に失敗しました: %s\n", strerror(errno));
	}
	else if (err_num == ERROR_BYTE_COUNT) {
		fprintf(stderr, "[E] 送信したフレームのバイト数が規定より少ないです: %zdバイト\n", num_write);
	}
	else {
		fprintf(stderr, "[E] 不明なエラーが発生しました\n");
	}
	return;
}
int send_frame(int s, struct can_frame *ptr_frame) {
	ssize_t num_write;
	num_write = write(s, ptr_frame, sizeof(struct can_frame));
	if (num_write == -1) {
		error_print(ERROR_WRITE, 0);
		return 1;
	}
	if (num_write < (ssize_t)sizeof(struct can_frame)) {
		error_print(ERROR_BYTE_COUNT, num_write);
		return 1;
	}
	return 0;
}
int send_sensor_data(int s, canid_t can_id, struct sensor_data *ptr_data) {
	struct can_frame frame = {0};
	frame.can_id = can_id;
	frame.len = sizeof(*ptr_data);
	memcpy(frame.data, ptr_data, sizeof(*ptr_data));
	return send_frame(s, &frame);
}

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

	/*>>> 1番目のフレーム(受信させる) <<<*/
	fprintf(stderr, "[D] 1番目のフレーム(受信予定)を送る準備をします\n");
	struct sensor_data data;
	data.temperature = 10;  /* 1byte */
	data.humidity = 10;  /* 1byte */
	data.pressure = 1010;  /* 2byte */
	data.timestamp = 0x1;  /* 4byte */
	int rt = send_sensor_data(s, 0x123, &data); 
	if (rt != 0) {
		close(s);
		return 1;
	}
	fprintf(stderr, "[D] 1番目のフレーム(受信予定)を送りました\n");
	
	/*>>> 2番目のフレーム(受信させない) <<<*/
	fprintf(stderr, "[D] 2番目のフレーム(受信させない)を送る準備をします\n");
	/* 0でリセット*/
	memset(&data, 0, sizeof(data));
	data.temperature = 20;  /* 1byte */
	data.humidity = 20;  /* 1byte */
	data.pressure = 1020;  /* 2byte */
	data.timestamp = 0x2;  /* 4byte */
	/* can idを変更(届かなくなるはず) */
	rt = send_sensor_data(s, 0x456, &data); 
	if (rt != 0) {
		close(s);
		return 1;
	}
	fprintf(stderr, "[D] 2番目のフレーム(受信させない)を送りました\n");

	/*>>> 3番目のフレーム(受信させない) <<<*/
	fprintf(stderr, "[D] 3番目のフレーム(受信させない)を送る準備をします\n");
	/* 0でリセット*/
	memset(&data, 0, sizeof(data));
	data.temperature = 30;  /* 1byte */
	data.humidity = 30;  /* 1byte */
	data.pressure = 1030;  /* 2byte */
	data.timestamp = 0x3;  /* 4byte */
	/* 拡張フラグをセット(届かなくなるはず) */
	rt = send_sensor_data(s, 0x123 | CAN_EFF_FLAG, &data); 
	if (rt != 0) {
		close(s);
		return 1;
	}
	fprintf(stderr, "[D] 3番目のフレーム(受信させない)を送りました\n");

	/*>>> 終了処理 <<<*/
	fprintf(stderr, "[D] ソケットを閉じて終了します\n");
	close(s);
	return 0;
}
```
- フィルタ設定が成功していたら1番目の送信データは受信させ、2番目と3番目は受信しないように設定した。

#### 実行結果
前回と同じようにネットワークインターフェースを設定してから受信プログラムを実行した。
その後、送信プログラムを**二回**実行した。

- 端末1(受信側)
```bash
$ ./build/receiver 
[D] 受信プログラムの開始、socket()を実行
# 以下は一回目の送信プログラム実行後
[D] フレームの読み取りが完了しました
[D] フレームの解釈を開始します
[D] 受け取ったデータを表示します
温度: 10 °C
湿度: 10 %
気圧: 1010 hPa
送信時刻: 1 ms
# 以下は二回目の送信プログラム実行後
[D] フレームの読み取りが完了しました
[D] フレームの解釈を開始します
[D] 受け取ったデータを表示します
温度: 10 °C
湿度: 10 %
気圧: 1010 hPa
送信時刻: 1 ms
# Ctrl+cで強制終了
^C
```

- 端末2(送信側)
```bash
# 一回目の送信
$ ./send 
[D] 送信プログラムの開始、socket()を実行
[D] 1番目のフレーム(受信予定)を送る準備をします
[D] 1番目のフレーム(受信予定)を送りました
[D] 2番目のフレーム(受信させない)を送る準備をします
[D] 2番目のフレーム(受信させない)を送りました
[D] 3番目のフレーム(受信させない)を送る準備をします
[D] 3番目のフレーム(受信させない)を送りました
[D] ソケットを閉じて終了します
# 二回目の送信
$ ./send 
[D] 送信プログラムの開始、socket()を実行
[D] 1番目のフレーム(受信予定)を送る準備をします
[D] 1番目のフレーム(受信予定)を送りました
[D] 2番目のフレーム(受信させない)を送る準備をします
[D] 2番目のフレーム(受信させない)を送りました
[D] 3番目のフレーム(受信させない)を送る準備をします
[D] 3番目のフレーム(受信させない)を送りました
[D] ソケットを閉じて終了します
```
- 2番目(can idを変更)と3番目(標準から拡張フレームに変更)が届かないことが確認できた
  - これによってフィルタ機能が働いていることが確認できた


### 感想
- ビット論理積によってフラグを判定したりフィルタリングするのはIPパケットでやったので理解がしやすかった。
- 今回はctrl+cでの強制終了で終わるようにしたが、、今度はctrl+cでループを抜けて今回到達しなかった
  終了処理を実行できるようにしたい。

## 第3回 送信テスト 2026-06-08
### 学習内容
- 送信プログラムの作成
- 送信・受信プログラムのテスト

#### 送信プログラムの作成
前回はcansendで代替したが、今回は送信プログラムを自作する。
以下のようなプログラムを作成した。(send.c)
```c
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
```

#### 実行結果
前回と同様にネットワークインターフェースを作成した後に端末1、端末2でそれぞれ受信・送信プログラムを実行する。

- 端末1で受信プログラムを実行した後、端末2で送信プログラムを実行すると以下のように表示された。
```bash
# 端末2(送信側)
$ ./send
[D] 送信プログラムの開始、socket()を実行
[D] ソケットを閉じて終了します
```
- 端末2からの送信後、受信側の端末1の画面は以下のようになった。
```bash
# 端末1(受信側)の画面
$ ./build/receiver 
[D] 受信プログラムの開始、socket()を実行
# 以下は端末1で送信実行後に表示
[D] フレームの読み取りが完了しました
[D] フレームの解釈を開始します
[D] 受け取ったデータを表示します
温度: 25 °C
湿度: 50 %
気圧: 1013 hPa
送信時刻: 305441741 ms
```

### 感想
- 受信プログラムとほぼ同じだったので後半のフレーム組み立てだけ注意して取り組んだ。
  - 特にバイトオーダーとパディングに気をつけたい。今後はエンディアンを調整する関数も
    使って移植性なども意識していきたい。

## 第2回 受信テスト 2026-06-07
### 学習内容
- ネットワークインターフェース関連の基本コマンドの確認
- 受信側のプログラムの作成

#### 基本コマンドの確認
基本のコマンドからまとめる。
- ネットワークインターフェースの確認
```bash
$ ip link show 
```
- 仮想CANインターフェースのカーネルモジュールをロード
```bash
$ sudo modprobe vcan
```
- ネットワークインターフェースの追加
```bash
$ sudo ip link add dev [インターフェース名(vcan0)] type [種別(vcan)]
```
- ネットワークインターフェースの削除
```bash
$ sudo ip link delete [インターフェース名(vcan0)]
```
- インターフェースのup/down
```bash
$ sudo ip link set [インターフェース名(van0)] [up|down]
```

#### 仮想CANインターフェースの作成
```bash
$ sudo modprobe vcan
$ sudo ip link add dev vcan0 type vcan
$ ip link show vcan0
3: vcan0: <NOARP> mtu 2060 qdisc noop state DOWN mode DEFAULT group default qlen 1000
    link/can 
$ sudo ip link set vcan0 up
$ ip link show vcan0
3: vcan0: <NOARP,UP,LOWER_UP> mtu 2060 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000
    link/can 
```
- これで仮想インターフェースは作成したので、candumpでテストする。

- 端末1で以下のコマンドを実行した
```bash
# 端末1で実行
$ candump vcan0  # 待機状態になる
```
- 次に、端末2で以下のコマンドを実行した
```bash
# 端末2で実行
$ cansend vcan0 123#1234ABCD
```
- その後、待機状態の端末1の画面は以下のような表示になった
```bash
# 端末1の表示
$ candump vcan0
  vcan0  123   [4]  12 34 AB CD
```
- これで仮想インターフェースが機能していることが確認できた。

#### RAWフレームの受信プログラムの作成
- 以下は受け渡すデータ用のヘッダファイル(util.h)
```c
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
```
- 以下は受信側のプログラム(receive.c)
```c
/*
 * 生の仮想CANフレームを受信するプログラム
 * 
 * ソケットを開けてから受信までの処理はほぼ学習メモ第1回の内容通り。
 * それに加えて読んだフレームを独自の構造体にキャストして意味のあるデータとして
 * 解釈できるようにする。今回は単純化のため一つのフレームで一つのデータとして
 * 成立するようにする。(8バイトで収まるデータにして、複数フレームからの組み立ては不要にする)
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
	printf("温度: %d\n"
	       "湿度: %u\n"
	       "気圧: %u\n"
	       "送信時刻: %u\n",
	       data.temperature,
	       data.humidity,
	       data.pressure,
	       data.timestamp);

	/*>>> 終了処理 <<<*/
	fprintf(stderr, "[D] ソケットを閉じて終了します\n");
	close(s);
	return 0;
}
```
- 送信側については今回はcansendで代用する。(次回送信側のプログラムを作成)
cansendで送るテストデータを以下のように決めた。
- リトルエンディアンで送ることにしたので、バイトの並び順に注意する必要がある。
```text
データの種類    10進数表示  16進数表示  実際に送るバイト列
温度(1byte)     25          19          19
湿度(1byte)     50          32          32
気圧(2byte)     1013        03F5        F5 03
送信時刻(4byte) 305441741   1234ABCD    CD AB 34 12
```
この場合、以下のようにすればデータを送信できる。
```bash
$ cansend vcan0 123#1932F503CDAB3412
```

#### 実行結果
仮想インターフェース(vcan0)を作成した状態で受信側のプログラムを起動したあと、別端末でcansendを実行する。

- まず端末1で受信側のプログラムを実行する
```bash
# 端末1
$ ./build/receiver
[D] 受信プログラムの開始、socket()を実行
```
- 次に端末2から以下のようにcansendを実行する
```bash
# 端末2
$ cansend vcan0 123#1932F503CDAB3412
```
- その後、端末1の画面が以下のようになった。
```bash
# 端末1の画面
$ ./build/receiver
[D] 受信プログラムの開始、socket()を実行
# 以下は受信後に表示
[D] フレームの読み取りが完了しました
[D] フレームの解釈を開始します
[D] 受け取ったデータを表示します
温度: 25
湿度: 50
気圧: 1013
送信時刻: 305441741
[D] ソケットを閉じて終了します
```
### 感想
- バイトオーダー順は間違えると異常データになるので仕様をよくチェックするよう心がけたい。
- TCP/IPの実装でフレーム・パケットのやりとりを理解していたのでスムーズに理解できた。
- 受け取ったデータの表示に単位をつけるのを忘れていたので、次回までに改善する予定。

## 第1回 CANの基礎理解 2026-06-06
### 学習内容
- 以下のコマンドで必要ツールをインストールした。
```bash
sudo dnf install can-utils
```
- 学習の参考に以下のリンクとヘッダファイルを確認することにした。
  - https://docs.kernel.org/networking/can.html
  - /usr/include/linux/can.h
  - /usr/include/linux/can/raw.h

- まずhttps://docs.kernel.org/networking/can.htmlの内容をまとめる。

#### [ドキュメント](https://docs.kernel.org/networking/can.html)のまとめ
上記リンク先からコードを引用しています。
##### SocketCANが生まれた経緯
これまでのCAN実装には以下のような問題があった。
- キャラクタデバイス(/dev以下のデバイスファイル)とopenなどでやりとりしていて、できることが限定される
- 複数のプロセスが同じデバイスにアクセスすることができない
- フレームのキューイングやISO-TP(CANフレームの8バイトのペイロードを超えるデータを分割・再構築する規格)
  をユーザ空間に自力で実装する必要がある
- CANコントローラを変えるたびに新しいドライバのAPIに変わってしまうので、その度にプログラムを変更する必要がある

まとめると抽象化層がなく、移植性が低かった。
そこで、Linuxカーネルのネットワーク層を利用することになった。
具体的にはCANコントローラをネットワークデバイスとして扱うことでLinuxのネットワーク層を
利用できるようにした。
これにより、以下のようなことが可能になった。
- ネットワーク層のキューイングを利用できる
- 複数のプロセスが同じデバイスにアクセス(同じCAN IDのフレームを受信)できる
- 複数のトランスポートプロトコルを動的にロード・アンロードできる
- プロトコルを選択するだけでフレームを気にせずバイト列を書き込むだけでよくなる(抽象化)

また、キャラクタデバイスを直接扱う方式だと以下の問題がある。
- ioctl()を多用する必要があるためコードが複雑化する
- Linuxのキューイングをそのまま利用できないのでわざわざ複製してくる必要がある
- 抽象化する層を作る必要がある
これらの問題から、Linuxカーネルのネットワーク層を使った(socket()やbind()などを使う)実装の方が優れていると判断され、
SocketCANが作成された。

##### SocketCANの仕組み
- CANバスでは常にブロードキャストで送信する(EthernetのようにMACアドレスを指定しなくて良い)
- CAN IDは一意に決定される(調停の仕組みがあるため)
  - 調停とは、データ送信の優先権の決め方で、最も小さいCAN IDのデータが優先して送られる
- プロセスがCAN IDのデータを要求すると、RAWプロトコルモジュールがそれをSocketCANコアモジュールのリストに登録する
- ローカルループバック
  - 同じマシンの別プロセス(ソケット)にもメッセージを送りたいとき使う
  - 調停の存在から、ループバックは送信成功後に実行する必要がある(実際の送信順と順番が変わってしまう)
  - CANネットワークインターフェースまたはSocketCANコアがローカルループバックを実行する
- エラー検査機能
  - 物理層・メディアアクセス制御層でのエラーは、通常のCANフレームと同様に送信される
  - エラーを受け取るにはフィルタ設定をする必要がある
  - エラーフォーマットについては include/uapi/linux/can/error.h を参照

##### SocketCANの使い方
- TCP/IPと同じように、まずはソケットを開く(socket()、bind()などについてはTLPI p1217~も参考)
- rawソケットプロトコルの場合以下のように書く
```c
/*
 * int domain: PF_CAN  - プロトコルファミリ(CAN)
 * int type: SOCK_RAW  - ソケットの種類(生のフレームを扱う)
 * int protocol: CAN_RAW  - 具体的なプロトコル (RAWプロトコル)
 */
s = socket(PF_CAN, SOCK_RAW, CAN_RAW); 
```
- ブロードキャストマネージャー(BCM)のときは以下のように書く
```c
/*
 * int domain: PF_CAN  - プロトコルファミリ(CAN)
 * int type: SOCK_DGRAM  - ソケットの種類(データグラムソケット(TLPI p1216))
 * int protocol: CAN_BCM  - 具体的なプロトコル (BCMプロトコル)
 */
s = socket(PF_CAN, SOCK_DGRAM, CAN_BCM);
```
- CANフレームは以下の構造体になっている(/usr/include/linux/can.h)
```c
struct can_frame {
        /* can_id: フラグとCAN IDを32bitに格納したもの */
        canid_t can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
        /* unionなのでデータ長としてlenとcan_dlcはどちらでも使えるが、can_dlcは廃止されたのでlenを推奨 */
        union {
                /* CAN frame payload length in byte (0 .. CAN_MAX_DLEN)
                 * was previously named can_dlc so we need to carry that
                 * name for legacy support
                 */
                __u8 len;
                __u8 can_dlc; /* deprecated */
        };
        /* パディング用 */
        __u8    __pad;   /* padding */
        /* 予約領域(使わない) */
        __u8    __res0;  /* reserved / padding */
        /* 8バイトを超えたときに使えるが、基本的に使わない? */
        __u8    len8_dlc; /* optional DLC for 8 byte payload length (9 .. 15) */
        /*
         * 実データ(8byte) 
         * 独自の構造体にキャストして扱えるよう、メモリ配置のアドレスを8バイトの境界になるよう設定
         * (キャスト後の型(8byte)によって未整列アクセスが起きるのを防ぐため)
         * バイトオーダーは決まっていない(使用者が決める必要がある) 
         */
        __u8    data[8] __attribute__((aligned(8)));
};
```
- sockaddr_can構造体
```c
struct sockaddr_can {
        /* CANの番号を入れる */
        sa_family_t can_family;
        /* インターフェースの番号(vcan0など)、番号をioctl()で特定する必要がある */
        int         can_ifindex;
        /*
         * プロトコルごとのアドレス情報
         * RAWの場合はフレームを直接読んでcan_idを見るので不要
         */
        union {
                /* transport protocol class address info (e.g. ISOTP) */
                struct { canid_t rx_id, tx_id; } tp;

                /* J1939 address information */
                struct {
                        /* 8 byte name when using dynamic addressing */
                        __u64 name;

                        /* pgn:
                         * 8 bit: PS in PDU2 case, else 0
                         * 8 bit: PF
                         * 1 bit: DP
                         * 1 bit: reserved
                         */
                        __u32 pgn;

                        /* 1 byte address */
                        __u8 addr;
                } j1939;

                /* reserved for future CAN protocols address information */
        } can_addr;
};
```
- 生のフレームをやり取りできるようsocket()とbind()を実行する
  - 実際に使うときはエラーチェックの処理を追加する
```c
int s;
struct sockaddr_can addr;
struct ifreq ifr;

/* 生のフレームを受け取るソケットを設定 */
s = socket(PF_CAN, SOCK_RAW, CAN_RAW);

/* インターフェースの名前にcan0を設定 */
strcpy(ifr.ifr_name, "can0" );
/* インターフェースのインデックス(番号)を調べる */
ioctl(s, SIOCGIFINDEX, &ifr);

/* sockaddr_canに設定していく */
addr.can_family = AF_CAN;
/* ifr.ifr_ifindexにさっき調べたインデックス番号が入っている */
addr.can_ifindex = ifr.ifr_ifindex;

/* bindの引数の型にあわせてキャストする */
bind(s, (struct sockaddr *)&addr, sizeof(addr));
```
- bind()したあと読み取るには以下のようにする
```c
/* フレームを受け取る箱 */
struct can_frame frame;

/* readでフレームを受け取る */
nbytes = read(s, &frame, sizeof(struct can_frame));

/* 読み取れなければエラー */
if (nbytes < 0) {
        perror("can raw socket read");
        return 1;
}

/* もしバイト数が規定(16byte)より少なかったらエラー */
/* paranoid check ... */
if (nbytes < sizeof(struct can_frame)) {
        fprintf(stderr, "read: incomplete CAN frame\n");
        return 1;
}

/* do something with the received CAN frame */
```
- 書き込みはwriteを使って以下のように書く
```c
nbytes = write(s, &frame, sizeof(struct can_frame));
```
### 感想
- 学んだこと
  - 抽象化は移植性を高めて労力を減らすという点で優れていると思った。 
  - Linuxカーネルのネットワーク層をそのまま使えるのは便利だと思った。
  - CANはブロードキャストでフレームを送り、受信側が生のフレームを受け取る設定なら
  受信側はフレームのcan_idを直接確認してから取捨選択をしているという流れが理解できた。
- 次回は今回学んだことを思い出しながら実験コードを書いていく予定。
