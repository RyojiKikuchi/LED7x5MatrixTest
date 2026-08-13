/*
 * HT16K33A LED 7x21 Matrix Display Test
 * 
 * UARTからの入力を7x21のディスプレイに出力する
 * 
 */

/*
 * MAIN Generated Driver File
 * 
 * @file main.c
 * 
 * @defgroup main MAIN
 * 
 * @brief This is the generated driver implementation file for the MAIN driver.
 *
 * @version MAIN Driver Version 1.0.2
 *
 * @version Package Version: 3.1.2
 */

/*
? [2026] Microchip Technology Inc. and its subsidiaries.

    Subject to your compliance with these terms, you may use Microchip 
    software and any derivatives exclusively with Microchip products. 
    You are responsible for complying with 3rd party license terms  
    applicable to your use of 3rd party software (including open source  
    software) that may accompany Microchip software. SOFTWARE IS ?AS IS.? 
    NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS 
    SOFTWARE, INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT,  
    MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT 
    WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY 
    KIND WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF 
    MICROCHIP HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE 
    FORESEEABLE. TO THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP?S 
    TOTAL LIABILITY ON ALL CLAIMS RELATED TO THE SOFTWARE WILL NOT 
    EXCEED AMOUNT OF FEES, IF ANY, YOU PAID DIRECTLY TO MICROCHIP FOR 
    THIS SOFTWARE.
 */
#include "mcc_generated_files/system/system.h"
#include <string.h>

#define HT16K33_STANDBY_MODE 0x20U
#define HT16K33_NORMAL_OPERATION_MODE 0x21U

#define HT16K33_DISPLAY_OFF 0x80U
#define HT16K33_DISPLAY_ON_BLINK_OFF 0x81U
#define HT16K33_DISPLAY_ON_BLINK_2HZ 0x83U
#define HT16K33_DISPLAY_ON_BLINK_1HZ 0x85U
#define HT16K33_DISPLAY_ON_BLINK_0_5HZ 0x87U

#define HT16K33_ROWINT_ROW 0xA0U
#define HT16K33_ROWINT_INT_ACTIVE_LOW 0xA1U
#define HT16K33_ROWINT_INT_ACTIVE_HIGH 0xA3U

#define HT16K33_DIMMING 0xE0U

#define I2C_TIME_OUT_TMR0 15U   // 4.1 * I2C_TIME_OUT_TMR0 (30ms以上あれば安全)

/*
 * UARTバッファサイズの最大値について
 * 内部のカウンタが8bitなので、最大値を超えないように考慮
 * scroll_posが一番大きくなる。
 * 256/5=51.2 余裕を見て48程度を最大値とする。
 */
#define UART_BUFFER_SIZE 48U     // シリアル通信の受信バッファサイズ

#define DISP_SLAVE_ADDRESS 0x70U      // I2C スレーブアドレス
#define DISP_SCROLL_TMR0 60U    // スクロールスピード(4.1ms * DISP_SCROLL_TMP0)
#define DISP_SCROLL_SPACE_STRING "\x82\x82"  // スクロール時の隙間この間隔+1のスペースが空く
#define ROW_COUNT 5U
#define COL_COUNT 21U

#define ROW_BUFFER_LENGTH 3U    // バッファ数、バッファ数x8bitがディスプレイバッファのビット数
#define ROW_BUFFER_BITS ((uint8_t) (ROW_BUFFER_LENGTH * 8))

#define SET_DISP_BUFFER_FULLWRITE_SPACE 0U
#define SET_DISP_BUFFER_OVERFLOW 1U
#define SET_DISP_BUFFER_FULLWRITE_NOSPACE 2U

static char uart_buf[UART_BUFFER_SIZE]; // シリアル通信受信バッファ
static char disp_char_buf[UART_BUFFER_SIZE]; // 内部バッファ
static bool need_scroll = false;
static uint8_t scroll_pos = 0; // スクロール位置
static uint8_t skip_count = 0;

typedef struct {
    uint8_t disp_bits[ROW_COUNT];
    uint8_t bit_length;
    bool no_space;
} disp_char_data_t;

typedef struct {
    uint8_t bytes[ROW_BUFFER_LENGTH]; // 配列アクセス用
} disp_row_t;

static disp_row_t disp_buffer[ROW_COUNT]; // 表示用バッファ
static uint8_t disp_buffer_length = 0; // 格納済みのbit数

static uint8_t disp_led = 0U; // LED点灯制御
static bool i2c_error = true; // I2Cエラー有無(初回にi2c_recoveryを呼ぶためにtrue設定)

static uint8_t disp_brightness = 0x0FU;


// キャラクタデータ
static const uint8_t disp_data[][3] = {
    {0x20U, 0x00U, 0x00U}, // 20  
    {0x28U, 0x88U, 0x08U}, // 21 !
    {0x6AU, 0xA0U, 0x00U}, // 22 "
    {0x86U, 0xF6U, 0xF6U}, // 23 #
    {0x84U, 0x7FU, 0xE4U}, // 24 $
    {0x80U, 0x92U, 0x49U}, // 25 %
    {0x86U, 0x97U, 0xA7U}, // 26 &
    {0x4CU, 0x40U, 0x00U}, // 27 '
    {0x44U, 0x88U, 0x84U}, // 28 (
    {0x48U, 0x44U, 0x48U}, // 29 )
    {0x64U, 0xEEU, 0xE4U}, // 2A *
    {0x60U, 0x4EU, 0x40U}, // 2B +
    {0x40U, 0x00U, 0xC4U}, // 2C ,
    {0x60U, 0x0EU, 0x00U}, // 2D -
    {0x20U, 0x00U, 0x08U}, // 2E .
    {0x60U, 0x24U, 0x80U}, // 2F /
    {0x86U, 0x99U, 0x96U}, // 30 0
    {0x82U, 0x62U, 0x2FU}, // 31 1
    {0x8EU, 0x16U, 0x8FU}, // 32 2
    {0x8EU, 0x16U, 0x1EU}, // 33 3
    {0x89U, 0x97U, 0x11U}, // 34 4
    {0x8FU, 0x8EU, 0x1EU}, // 35 5
    {0x86U, 0x8EU, 0x96U}, // 36 6
    {0x8FU, 0x12U, 0x22U}, // 37 7
    {0x86U, 0x96U, 0x96U}, // 38 8
    {0x86U, 0x97U, 0x16U}, // 39 9
    {0x20U, 0x80U, 0x80U}, // 3A :
    {0x40U, 0x40U, 0x48U}, // 3B ;
    {0x62U, 0x48U, 0x42U}, // 3C <
    {0x60U, 0xE0U, 0xE0U}, // 3D =
    {0x68U, 0x42U, 0x48U}, // 3E >
    {0x86U, 0x92U, 0x02U}, // 3F ?
    {0x86U, 0x9BU, 0xB6U}, // 40 @
    {0x86U, 0x9FU, 0x99U}, // 41 A
    {0x8EU, 0x9EU, 0x9EU}, // 42 B
    {0x87U, 0x88U, 0x87U}, // 43 C
    {0x8EU, 0x99U, 0x9EU}, // 44 D
    {0x8FU, 0x8EU, 0x8FU}, // 45 E
    {0x8FU, 0x8EU, 0x88U}, // 46 F
    {0x87U, 0x8BU, 0x97U}, // 47 G
    {0x89U, 0x9FU, 0x99U}, // 48 H
    {0x6EU, 0x44U, 0x4EU}, // 49 I
    {0x87U, 0x11U, 0x96U}, // 4A J
    {0x89U, 0xACU, 0xA9U}, // 4B K
    {0x88U, 0x88U, 0x8FU}, // 4C L
    {0x89U, 0xFFU, 0x99U}, // 4D M
    {0x89U, 0xDBU, 0x99U}, // 4E N
    {0x86U, 0x99U, 0x96U}, // 4F O
    {0x8EU, 0x9EU, 0x88U}, // 50 P
    {0x86U, 0x99U, 0xB7U}, // 51 Q
    {0x8EU, 0x9EU, 0xA9U}, // 52 R
    {0x87U, 0x86U, 0x1EU}, // 53 S
    {0x6EU, 0x44U, 0x44U}, // 54 T
    {0x89U, 0x99U, 0x96U}, // 55 U
    {0x89U, 0x99U, 0x52U}, // 56 V
    {0x89U, 0x9FU, 0xF9U}, // 57 W
    {0x89U, 0x96U, 0x99U}, // 58 X
    {0x6AU, 0xA4U, 0x44U}, // 59 Y
    {0x8FU, 0x16U, 0x8FU}, // 5A Z
    {0x4CU, 0x88U, 0x8CU}, // 5B [
    {0x6AU, 0x4EU, 0xE4U}, // 5C ￥
    {0x4CU, 0x44U, 0x4CU}, // 5D ]
    {0x64U, 0xA0U, 0x00U}, // 5E ^
    {0x80U, 0x00U, 0x0FU}, // 5F _
    {0x48U, 0x40U, 0x00U}, // 60 `
    {0x8EU, 0x17U, 0x97U}, // 61 a
    {0x88U, 0x8EU, 0x9EU}, // 62 b
    {0x60U, 0x68U, 0x86U}, // 63 c
    {0x81U, 0x17U, 0x97U}, // 64 d
    {0x86U, 0x9FU, 0x86U}, // 65 e
    {0x62U, 0x4EU, 0x44U}, // 66 f
    {0x86U, 0x97U, 0x16U}, // 67 g
    {0x68U, 0x8CU, 0xAAU}, // 68 h
    {0x28U, 0x08U, 0x88U}, // 69 i
    {0x44U, 0x04U, 0x48U}, // 6A j
    {0x80U, 0x9AU, 0xE9U}, // 6B k
    {0x48U, 0x88U, 0x8CU}, // 6C l
    {0x80U, 0x5BU, 0xBBU}, // 6D m
    {0x80U, 0xE9U, 0x99U}, // 6E n
    {0x80U, 0x69U, 0x96U}, // 6F o
    {0x86U, 0x9EU, 0x88U}, // 70 p
    {0x86U, 0x97U, 0x11U}, // 71 q
    {0x80U, 0xBCU, 0x88U}, // 72 r
    {0x86U, 0x86U, 0x16U}, // 73 s
    {0x60U, 0x4EU, 0x42U}, // 74 t
    {0x80U, 0x99U, 0x97U}, // 75 u
    {0x80U, 0x99U, 0x52U}, // 76 v
    {0x80U, 0xBBU, 0xB6U}, // 77 w
    {0x80U, 0x96U, 0x69U}, // 78 x
    {0x89U, 0x97U, 0x1EU}, // 79 y
    {0x80U, 0xF2U, 0x4FU}, // 7A z
    {0x62U, 0x4CU, 0x42U}, // 7B {
    {0x28U, 0x88U, 0x88U}, // 7C |
    {0x68U, 0x46U, 0x48U}, // 7D }
    {0x80U, 0x05U, 0xA0U}, // 7E ~
    {0x8FU, 0xFFU, 0xFFU}, // 7F 
    {0x40U, 0x83U, 0x43U}, // 80 ℃
    {0x60U, 0x86U, 0x86U}, // 81 ℃
    {0x30U, 0x00U, 0x00U}, // 82 スクロール時のスペース用文字
};

#define DISP_DATA_COUNT (uint8_t) ((sizeof(disp_data) / sizeof((disp_data)[0])) -1)

/*
    Main application
 */

/*
 * UARTに出力する
 */
static void uart_write(const char *buf) {
    while (*buf != '\0') {
        while (!EUSART1_IsTxReady());
        EUSART1_Write(*(buf++));
    }
}

/*
 * I2Cリカバリ
 * SCLをトグルしてI2Cデバイスを解放する
 */
static void i2c_recovery(void) {

    // 1. MSSPモジュールを一旦OFF
    SSP1CON1bits.SSPEN = 0;

    // 保留中のSSP1CON2制御ビット（SEN/RSEN/PEN/RCEN/ACKEN）をクリア
    SSP1CON2 = 0x00;

    // 2. ピンを一時的に手動制御（GPIO）に切り替えてバスを解放
    // SDA, SCLを出力モードに設定
    SDA_TRIS = 0;
    SDA_OD = 1;
    SCL_TRIS = 0;
    SCL_OD = 1;

    // スレーブがSDAをLowに保持している場合、SCLを最大9回振って
    // スレーブの内部状態をリセットさせる（バス・クリア・シーケンス）
    for (uint8_t i = 0; i < 9; i++) {
        SCL_PORT = 0;
        __delay_us(5);
        //RA4 = 1;
        SCL_PORT = 1;
        __delay_us(5);
        // もしSDAがHighに戻ったら（スレーブが解放したら）途中で抜けても良い
        //if (RA5 == 1) break;
        if ((SDA_PORT) != 0) break;
    }

    // 3. ストップ条件を擬似的に生成（SDAをLow→Highへ）
    // SDA Low
    SDA_PORT = 0;
    __delay_us(5);
    // SCL High
    SCL_PORT = 1;
    __delay_us(5);
    // SDA High
    SDA_PORT = 1;
    __delay_us(5);

    // 4. ピン設定をMSSP用に戻す
    SCL_TRIS = 1;
    SDA_TRIS = 1;

    // 5. WCOL（書き込み衝突）とSSPOV（受信オーバーフロー）エラーフラグをクリア
    //    これらはソフトウェアで明示的にクリアしないとSSPEN ON/OFFを跨いで残留する
    SSP1CON1 &= ~0xC0;

    // 6. MSSPモジュールを再起動
    SSP1CON1bits.SSPEN = 1;

    // 7. BF（Buffer Full）フラグのクリア
    //    I2C受信完了直後にタイムアウトした場合、SSP1BUFにデータが残りBF=1のままになる。
    //    BF=1だとI2C_Wait()が永久にタイムアウトし、以降の操作がすべてNG,TOになる。
    //    SSP1BUFを読み捨てることでBFをクリアする。
    if (SSP1STATbits.BF) {
        (void) SSP1BUF;
    }

    i2c_error = false;

}

/*
 * ディスプレイへ表示データを出力する
 */
static void i2c_puts(uint16_t slave_address, uint8_t *send_data, uint8_t length) {

    // 送信依頼
    if (!I2C1_Write(slave_address, send_data, length)) {
        i2c_error = true;
        return;
    }

    // Busyになるまでwait
    TMR0L = 0;
    while (!I2C1_IsBusy() && TMR0L < I2C_TIME_OUT_TMR0);

    // Busy解除までWait
    TMR0L = 0;
    while (I2C1_IsBusy()) {
        // タイムアウト
        if (TMR0L >= I2C_TIME_OUT_TMR0) {
            i2c_error = true;
            return;
        }
    }

    // エラーチェック
    if (I2C1_ErrorGet() != I2C_ERROR_NONE) {
        i2c_error = true;
    }

}

/*
 * キャラクタ情報を取得する
 */
static void get_char_data(const uint8_t *disp_data1, disp_char_data_t *disp_char_data) {

    uint8_t d0 = disp_data1[0];
    uint8_t d1 = disp_data1[1];
    uint8_t d2 = disp_data1[2];

    disp_char_data->bit_length = d0 >> 5; // 上位3bitがビット長
    disp_char_data->no_space = ((d0 & 0x10U) != 0U); // SPACE要否(1: SPACEを挿入しない)
    disp_char_data->disp_bits[0] = (uint8_t) (d0 << 4);
    disp_char_data->disp_bits[1] = d1 & 0xF0U;
    disp_char_data->disp_bits[2] = (uint8_t) (d1 << 4);
    disp_char_data->disp_bits[3] = d2 & 0xF0U;
    disp_char_data->disp_bits[4] = (uint8_t) (d2 << 4);

}

/*
 * キャラクタ情報をDisplayメモリに設定する
 *  return
 *   0: 全ビット設定 ＆ SPACE設定
 *   1: 全ビット設定不可
 *   2: 全ビット設定 ＆ SPACE未設定
 */
static uint8_t set_disp_buffer(uint8_t char_index) {

    // キャラクタデータ取得
    disp_char_data_t disp_char_data;
    get_char_data(disp_data[char_index], &disp_char_data);

    // スクロール反映
    if (need_scroll) {

        if (disp_char_data.bit_length <= (scroll_pos - skip_count)) {
            // 文字出力不要
            skip_count += disp_char_data.bit_length;
            if (disp_char_data.no_space) {
                return SET_DISP_BUFFER_FULLWRITE_NOSPACE;
            } else {
                // SPACE追加
                if (scroll_pos > skip_count) {
                    skip_count++;
                } else {
                    disp_buffer_length++;
                }
                return SET_DISP_BUFFER_FULLWRITE_SPACE;
            }
        } else if (skip_count < scroll_pos) {
            uint8_t shift = scroll_pos - skip_count;
            disp_char_data.bit_length -= shift;
            for (uint8_t row = 0; row < ROW_COUNT; row++) {
                disp_char_data.disp_bits[row] <<= shift;
            }
            skip_count += shift;
        }
    }

    uint8_t bits = disp_buffer_length;
    uint8_t idx = 0;
    while (bits >= 8) {
        idx++;
        bits -= 8;
    }

    // オーバーフロー
    if (idx >= ROW_BUFFER_LENGTH) {
        return SET_DISP_BUFFER_OVERFLOW;
    }

    // 設定先のインデックス、ビット位置、溢れビット計算
    uint8_t bit_len = disp_char_data.bit_length;
    uint8_t return_status = SET_DISP_BUFFER_FULLWRITE_SPACE;
    uint8_t r = 0;
    uint8_t next_pos = (bits + bit_len);
    if (next_pos > 8) {
        r = bit_len - (next_pos - 8);
        if ((idx + 1) >= ROW_BUFFER_LENGTH) {
            // 余り分を格納するスペース無し
            return_status = SET_DISP_BUFFER_OVERFLOW;
        }
    }

    // バッファに追加
    for (uint8_t row = 0; row < ROW_COUNT; row++) {
        disp_buffer[row].bytes[idx] |= disp_char_data.disp_bits[row] >> bits;
        // 溢れビットが存在するなら次のデータ位置に設定
        if (r && return_status == SET_DISP_BUFFER_FULLWRITE_SPACE) {
            disp_buffer[row].bytes[idx + 1] = (uint8_t) (disp_char_data.disp_bits[row] << r);
        }
    }

    // 設定数加算
    disp_buffer_length += bit_len;

    // スペース追加判定
    if (return_status == SET_DISP_BUFFER_FULLWRITE_SPACE) {
        if (disp_char_data.no_space) {
            return_status = SET_DISP_BUFFER_FULLWRITE_NOSPACE;
        } else if (disp_buffer_length < ROW_BUFFER_BITS) {
            // スペース挿入(1bit))
            disp_buffer_length++;
        } else {
            // スペースが挿入出来なかった
            return_status = SET_DISP_BUFFER_OVERFLOW;
        }
    }

    // 最大値超過していた場合最大値設定
    if (disp_buffer_length > ROW_BUFFER_BITS) {
        disp_buffer_length = ROW_BUFFER_BITS;
    }

    return return_status;
}

/*
 * ディスプレイメモリ(disp_buffer)をI2C送信用データに変換する。
 */
static void put_disp_buffer(void) {

    // バッファの構成は先頭1byteが開始アドレス(0x00)
    // 2byte以降が表示データ(16Byte)
    // 開始アドレスは全16バイト一括送信するので常に 0x00 
    uint8_t disp_raw_buffer[17];

    // バッファ初期化
    memset(disp_raw_buffer, 0x00U, sizeof (disp_raw_buffer));

    // 行のループ
    for (uint8_t row = 0U; row < ROW_COUNT; row++) {
        // 列のループ
        uint8_t bitmask = 0x80U;
        uint8_t buf_idx = 0;
        for (uint8_t col = 0U; col < COL_COUNT; col++) {

            /*
             * upper,lowerは以下変換表の上位4bitと下位4bit
             * 
             * DisplayメモリからI2C出力情報への変換表
             *  上位4bit : 配列のインデックス(0～15)
             *  下位4bit : ビット位置(0-7)
                static const uint8_t matrix_conv[5][21] = {
                    {0x07U, 0x27U, 0x47U, 0x67U, 0x87U, 0xA7U, 0xC7U, 0x02U, 0x22U, 0x42U, 0x62U, 0x82U, 0xA2U, 0xC2U, 0x15U, 0x35U, 0x55U, 0x75U, 0x95U, 0xB5U, 0xD5U},
                    {0x06U, 0x26U, 0x46U, 0x66U, 0x86U, 0xA6U, 0xC6U, 0x01U, 0x21U, 0x41U, 0x61U, 0x81U, 0xA1U, 0xC1U, 0x14U, 0x34U, 0x54U, 0x74U, 0x94U, 0xB4U, 0xD4U},
                    {0x05U, 0x25U, 0x45U, 0x65U, 0x85U, 0xA5U, 0xC5U, 0x00U, 0x20U, 0x40U, 0x60U, 0x80U, 0xA0U, 0xC0U, 0x13U, 0x33U, 0x53U, 0x73U, 0x93U, 0xB3U, 0xD3U},
                    {0x04U, 0x24U, 0x44U, 0x64U, 0x84U, 0xA4U, 0xC4U, 0x17U, 0x37U, 0x57U, 0x77U, 0x97U, 0xB7U, 0xD7U, 0x12U, 0x32U, 0x52U, 0x72U, 0x92U, 0xB2U, 0xD2U},
                    {0x03U, 0x23U, 0x43U, 0x63U, 0x83U, 0xA3U, 0xC3U, 0x16U, 0x36U, 0x56U, 0x76U, 0x96U, 0xB6U, 0xD6U, 0x11U, 0x31U, 0x51U, 0x71U, 0x91U, 0xB1U, 0xD1U}
                };
             */

            uint8_t upper = (col % 7) * 2 + ((col >= 14) ? 1 : 0);
            uint8_t lower;
            if (col < 7) {
                // 左ブロック（col 0〜6）
                lower = 7 - row;
            } else if (col < 14) {
                // 真ん中ブロック（col 7〜13）
                if (row < 3) {
                    lower = 2 - row;
                } else {
                    upper++;
                    lower = 10 - row;
                }

            } else {
                // 右ブロック（col 14〜20）
                lower = 5 - row;
            }

            uint8_t idx = upper + 1;
            uint8_t bit_pos = lower;

            uint8_t led_on = disp_buffer[row].bytes[buf_idx] & bitmask;
            if (led_on) {
                disp_raw_buffer[idx] |= 0x80U >> bit_pos;
            }
            bitmask >>= 1;
            if (bitmask == 0U) {
                buf_idx++;
                bitmask = 0x80U;
            }
        }
    }

    if (disp_led & 0x80U) {
        disp_raw_buffer[10] |= 0x80U;
    }
    if (disp_led & 0x40U) {
        disp_raw_buffer[14] |= 0x80U;
    }
    if (disp_led & 0x20U) {
        disp_raw_buffer[12] |= 0x80U;
    }

    i2c_puts(DISP_SLAVE_ADDRESS, disp_raw_buffer, sizeof (disp_raw_buffer));
}

static void disp_buffer_clear(void) {
    disp_buffer_length = 0;
    skip_count = 0;
    memset(disp_buffer, 0x00U, sizeof (disp_buffer));
}

/*
 * UARTで受信した文字をDisplayに設定する
 */
static void disp_write(const char *disp_message) {
    uint8_t status = SET_DISP_BUFFER_FULLWRITE_NOSPACE;

    while (*disp_message != '\0') {
        uint8_t char_index = ((uint8_t)*(disp_message++)) - 0x20U;
        if (char_index > DISP_DATA_COUNT) {
            continue;
        }

        status = set_disp_buffer(char_index);
        if (status == SET_DISP_BUFFER_OVERFLOW || disp_buffer_length >= ROW_BUFFER_BITS) {
            break;
        }
    }

    // 最後のスペースを削除する
    if (status == SET_DISP_BUFFER_FULLWRITE_SPACE && disp_buffer_length && !need_scroll) {
        disp_buffer_length--;
    }

    if (scroll_pos == 0 && need_scroll == false) {
        if (disp_buffer_length > COL_COUNT) {
            need_scroll = true;
        }
    }

}

static void rotate_disp_buf(void) {

    // 1ドットスクロールして出力
    scroll_pos++;
    disp_buffer_clear();
    disp_write(disp_char_buf);

    // スペース出力
    disp_write(DISP_SCROLL_SPACE_STRING);

    // 出力がなくなったらスクロール位置を0に戻す
    if (!disp_buffer_length) {
        scroll_pos = 0;
    }

    // 続きの出力
    if (disp_buffer_length <= COL_COUNT) {
        disp_write(disp_char_buf);
    }

}

/*
 * ディスプレイへ1byte出力する
 */
static void disp_put(uint8_t write_data) {
    __delay_ms(1);
    i2c_puts(DISP_SLAVE_ADDRESS, &write_data, 1);
}

static void disp_set_brightness(uint8_t brightness) {
    disp_put(HT16K33_DIMMING | (brightness & 0x0FU));
}

/*
 * HT16K33A初期化
 */
static void disp_init(void) {
    // オシレータ起動
    disp_put(HT16K33_NORMAL_OPERATION_MODE);
    // Display OFF
    disp_put(HT16K33_DISPLAY_OFF);
    // ROW/INTをROWに設定
    disp_put(HT16K33_ROWINT_ROW);
    // 明るさ設定
    disp_set_brightness(disp_brightness);
    // Display ON
    disp_put(HT16K33_DISPLAY_ON_BLINK_OFF);

}

/*
 * UARTから改行コードまで取得する
 */
static void uart_read_line(void) {
    uint8_t idx = 0;
    char c;
    TMR0L = 0;
    while (1) {
        while (!EUSART1_IsRxReady()) {
            // スクロール間隔判定
            if (TMR0L > DISP_SCROLL_TMR0) {
                TMR0L = 0;
                if (need_scroll) {
                    LED_SetHigh();
                    rotate_disp_buf();
                    put_disp_buffer();
                    LED_SetLow();
                }
            }
        }
        c = (char) EUSART1_Read();
        switch (c) {
            case '\r':
            case '\n':
                /*  CR/LF ends the line */
                if (idx == 0) continue; /* skip leading CR/LF */
                uart_buf[idx] = '\0';
                return;
        }
        if (idx < (uint8_t) (UART_BUFFER_SIZE - 1U)) {
            uart_buf[idx++] = c;
        }
    }
}

int main(void) {

    SYSTEM_Initialize();
    // If using interrupts in PIC18 High/Low Priority Mode you need to enable the Global High and Low Interrupts 
    // If using interrupts in PIC Mid-Range Compatibility Mode you need to enable the Global and Peripheral Interrupts 
    // Use the following macros to: 

    // Enable the Global Interrupts 
    INTERRUPT_GlobalInterruptEnable();

    // Disable the Global Interrupts 
    //INTERRUPT_GlobalInterruptDisable(); 

    // Enable the Peripheral Interrupts 
    INTERRUPT_PeripheralInterruptEnable();

    // Disable the Peripheral Interrupts 
    //INTERRUPT_PeripheralInterruptDisable(); 

    while (1) {
        if (i2c_error) {
            for (uint8_t i = 0; i < 4; i++) {
                LED_Toggle();
                __delay_ms(100);
            }
            i2c_recovery();
            disp_init();
        }

        LED_SetLow();
        uart_read_line();
        LED_SetHigh();

        for (uint8_t i = 0; i < UART_BUFFER_SIZE; i++) {
            disp_char_buf[i] = uart_buf[i];
        }

        uart_write(uart_buf);
        uart_write("\r\n");

        // スクロール位置をリセット
        need_scroll = false;
        scroll_pos = 0;

        disp_buffer_clear();
        disp_write(disp_char_buf);
        put_disp_buffer();

    }
}
