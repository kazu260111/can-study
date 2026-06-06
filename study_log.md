# 目的
- 車載ネットワークとして使われるCANについて自分でプログラムを作成しながら理解を深める

# 参考書籍
- **Linuxプログラミングインターフェース**
  - 著者: Michael Kerrisk
  - 訳者: 千住　治郎
  - 出版社: オライリー・ジャパン
本文中ではTLPIと表記


# 参考資料
[SocketCAN - Controller Area Network(Linuxカーネル)](https://docs.kernel.org/networking/can.html)

# 学習メモ
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
