/*----------------------------------------------------------------------------/
  Lovyan GFX - Graphics library for embedded devices.

Original Source:
 https://github.com/lovyan03/LovyanGFX/

Licence:
 [FreeBSD](https://github.com/lovyan03/LovyanGFX/blob/master/license.txt)

Author:
 [lovyan03](https://twitter.com/lovyan03)

Contributors:
 [ciniml](https://github.com/ciniml)
 [mongonta0716](https://github.com/mongonta0716)
 [tobozo](https://github.com/tobozo)
/----------------------------------------------------------------------------*/
#if defined (ESP32) || defined (CONFIG_IDF_TARGET_ESP32) || defined (CONFIG_IDF_TARGET_ESP32S2) || defined (ESP_PLATFORM)

#include "Bus_Parallel8.hpp"
#include "../../misc/pixelcopy.hpp"

#include <soc/dport_reg.h>

namespace lgfx
{
 inline namespace v1
 {
//----------------------------------------------------------------------------

  //#define SAFE_I2S_FIFO_WR_REG(i) (0x6000F000 + ((i)*0x1E000))
  //#define SAFE_I2S_FIFO_RD_REG(i) (0x6000F004 + ((i)*0x1E000))
  #define SAFE_I2S_FIFO_WR_REG(i) (0x3FF4F000 + ((i)*0x1E000))
  #define SAFE_I2S_FIFO_RD_REG(i) (0x3FF4F004 + ((i)*0x1E000))

  static constexpr std::uint32_t _conf_reg_default = I2S_TX_MSB_RIGHT | I2S_TX_RIGHT_FIRST | I2S_RX_RIGHT_FIRST;
  static constexpr std::uint32_t _conf_reg_start   = _conf_reg_default | I2S_TX_START;
  static constexpr std::uint32_t _sample_rate_conf_reg_32bit = 32 << I2S_TX_BITS_MOD_S | 32 << I2S_RX_BITS_MOD_S | 1 << I2S_TX_BCK_DIV_NUM_S | 1 << I2S_RX_BCK_DIV_NUM_S;
  static constexpr std::uint32_t _sample_rate_conf_reg_16bit = 16 << I2S_TX_BITS_MOD_S | 16 << I2S_RX_BITS_MOD_S | 1 << I2S_TX_BCK_DIV_NUM_S | 1 << I2S_RX_BCK_DIV_NUM_S;
  static constexpr std::uint32_t _fifo_conf_default = 1 << I2S_TX_FIFO_MOD | 1 << I2S_RX_FIFO_MOD | 32 << I2S_TX_DATA_NUM_S | 32 << I2S_RX_DATA_NUM_S;
  static constexpr std::uint32_t _fifo_conf_dma     = 1 << I2S_TX_FIFO_MOD | 1 << I2S_RX_FIFO_MOD | 32 << I2S_TX_DATA_NUM_S | 32 << I2S_RX_DATA_NUM_S | I2S_DSCR_EN;

  static __attribute__ ((always_inline)) inline volatile std::uint32_t* reg(std::uint32_t addr) { return (volatile std::uint32_t *)ETS_UNCACHED_ADDR(addr); }

  void Bus_Parallel8::config(const config_t& cfg)
  {
    _cfg = cfg;
    auto port = cfg.i2s_port;

    _i2s_sample_rate_conf_reg = reg(I2S_SAMPLE_RATE_CONF_REG(port));
    _i2s_fifo_conf_reg        = reg(I2S_FIFO_CONF_REG(port));
    _i2s_fifo_wr_reg          = reg(SAFE_I2S_FIFO_WR_REG(port));
    _i2s_state_reg            = reg(I2S_STATE_REG(port));
    _i2s_conf_reg             = reg(I2S_CONF_REG(port));

    _last_freq_apb = 0;
  }

  void Bus_Parallel8::init(void)
  {
    _init_pin();

    //Reset I2S subsystem
    *reg(I2S_CONF_REG(_cfg.i2s_port)) = I2S_TX_RESET | I2S_RX_RESET | I2S_TX_FIFO_RESET | I2S_RX_FIFO_RESET;
    *reg(I2S_CONF_REG(_cfg.i2s_port)) = _conf_reg_default;

    //Reset DMA
    *reg(I2S_LC_CONF_REG(_cfg.i2s_port)) = I2S_IN_RST | I2S_OUT_RST | I2S_AHBM_RST | I2S_AHBM_FIFO_RST;
    *reg(I2S_LC_CONF_REG(_cfg.i2s_port)) = I2S_OUT_EOF_MODE;

    *reg(I2S_CONF2_REG(_cfg.i2s_port)) = I2S_LCD_EN;

    *reg(I2S_CONF1_REG(_cfg.i2s_port))
          = I2S_TX_PCM_BYPASS
          | I2S_TX_STOP_EN
          ;

    *reg(I2S_CONF_CHAN_REG(_cfg.i2s_port))
          = 1 << I2S_TX_CHAN_MOD_S
          | 1 << I2S_RX_CHAN_MOD_S
          ;

    *reg(I2S_INT_ENA_REG(_cfg.i2s_port)) |= I2S_TX_REMPTY_INT_ENA | I2S_TX_WFULL_INT_ENA | I2S_TX_PUT_DATA_INT_ENA ;

    *reg(I2S_OUT_LINK_REG(_cfg.i2s_port)) = 0;
    *reg(I2S_IN_LINK_REG(_cfg.i2s_port)) = 0;
    *reg(I2S_TIMING_REG(_cfg.i2s_port)) = 0;

    _alloc_dmadesc(1);
    memset(_dmadesc, 0, sizeof(lldesc_t));
  }

  void Bus_Parallel8::_init_pin(void)
  {
    gpio_pad_select_gpio(_cfg.pin_d0);
    gpio_pad_select_gpio(_cfg.pin_d1);
    gpio_pad_select_gpio(_cfg.pin_d2);
    gpio_pad_select_gpio(_cfg.pin_d3);
    gpio_pad_select_gpio(_cfg.pin_d4);
    gpio_pad_select_gpio(_cfg.pin_d5);
    gpio_pad_select_gpio(_cfg.pin_d6);
    gpio_pad_select_gpio(_cfg.pin_d7);

    gpio_pad_select_gpio(_cfg.pin_rd);
    gpio_pad_select_gpio(_cfg.pin_wr);
    gpio_pad_select_gpio(_cfg.pin_rs);

    gpio_hi(_cfg.pin_rd);
    gpio_hi(_cfg.pin_wr);
    gpio_hi(_cfg.pin_rs);

    gpio_set_direction((gpio_num_t)_cfg.pin_rd, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)_cfg.pin_wr, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)_cfg.pin_rs, GPIO_MODE_OUTPUT);

    auto idx_base = (_cfg.i2s_port == I2S_NUM_0) ? I2S0O_DATA_OUT8_IDX : I2S1O_DATA_OUT8_IDX;
    gpio_matrix_out(_cfg.pin_rs, idx_base + 8, 0, 0);
    gpio_matrix_out(_cfg.pin_d7, idx_base + 7, 0, 0);
    gpio_matrix_out(_cfg.pin_d6, idx_base + 6, 0, 0);
    gpio_matrix_out(_cfg.pin_d5, idx_base + 5, 0, 0);
    gpio_matrix_out(_cfg.pin_d4, idx_base + 4, 0, 0);
    gpio_matrix_out(_cfg.pin_d3, idx_base + 3, 0, 0);
    gpio_matrix_out(_cfg.pin_d2, idx_base + 2, 0, 0);
    gpio_matrix_out(_cfg.pin_d1, idx_base + 1, 0, 0);
    gpio_matrix_out(_cfg.pin_d0, idx_base    , 0, 0);

    std::uint32_t dport_clk_en;
    std::uint32_t dport_rst;

    if (_cfg.i2s_port == I2S_NUM_0) {
      idx_base = I2S0O_WS_OUT_IDX;
      dport_clk_en = DPORT_I2S0_CLK_EN;
      dport_rst = DPORT_I2S0_RST;
    } else {
      idx_base = I2S1O_WS_OUT_IDX;
      dport_clk_en = DPORT_I2S1_CLK_EN;
      dport_rst = DPORT_I2S1_RST;
    }
    gpio_matrix_out(_cfg.pin_wr, idx_base, 1, 0); // WR (Write-strobe in 8080 mode, Active-low)

    DPORT_SET_PERI_REG_MASK(DPORT_PERIP_CLK_EN_REG, dport_clk_en);
    DPORT_CLEAR_PERI_REG_MASK(DPORT_PERIP_RST_EN_REG, dport_rst);
  }

  void Bus_Parallel8::_alloc_dmadesc(size_t len)
  {
    if (_dmadesc) heap_caps_free(_dmadesc);
    _dmadesc_len = len;
    _dmadesc = (lldesc_t*)heap_caps_malloc(sizeof(lldesc_t) * len, MALLOC_CAP_DMA);
  }

  void Bus_Parallel8::release(void)
  {  }

  void Bus_Parallel8::beginTransaction(void)
  {
    std::uint32_t freq_apb = getApbFrequency();
    if (_last_freq_apb != freq_apb)
    {
      _last_freq_apb = freq_apb;
      // clock = 80MHz(apb_freq) / I2S_CLKM_DIV_NUM
      // I2S_CLKM_DIV_NUM 4=20MHz  /  5=16MHz  /  8=10MHz  /  10=8MHz
      std::uint32_t div_num = std::min(32u, std::max(4u, 1 + (freq_apb / (1 + _cfg.freq_write))));
      _clkdiv_write =            I2S_CLKA_ENA
                    |            I2S_CLK_EN
                    |       1 << I2S_CLKM_DIV_A_S
                    |       0 << I2S_CLKM_DIV_B_S
                    | div_num << I2S_CLKM_DIV_NUM_S
                    ;
    }
    *reg(I2S_CLKM_CONF_REG(_cfg.i2s_port)) = _clkdiv_write;
    sendmode = sendmode_t::sendmode_32bit_nodma;
    *_i2s_sample_rate_conf_reg = _sample_rate_conf_reg_32bit;
    *_i2s_fifo_conf_reg = _fifo_conf_default;
  }

  void Bus_Parallel8::endTransaction(void)
  {
    wait_i2s();
  }

  void Bus_Parallel8::wait_i2s(void)
  {
    auto conf_reg = _conf_reg_default | I2S_TX_RESET | I2S_RX_RESET | I2S_RX_FIFO_RESET;
    while (!(*reg(I2S_STATE_REG(_cfg.i2s_port)) & I2S_TX_IDLE));
    *_i2s_conf_reg = conf_reg;
  }

  void Bus_Parallel8::wait(void)
  {
    auto conf_reg = _conf_reg_default | I2S_TX_RESET | I2S_RX_RESET | I2S_RX_FIFO_RESET;

    while (!(*reg(I2S_INT_RAW_REG(_cfg.i2s_port)) & I2S_TX_REMPTY_INT_RAW));
    *reg(I2S_INT_CLR_REG(_cfg.i2s_port)) = I2S_TX_REMPTY_INT_CLR;

    while (!(*reg(I2S_STATE_REG(_cfg.i2s_port)) & I2S_TX_IDLE));
    *_i2s_conf_reg = conf_reg;
  }

  bool Bus_Parallel8::busy(void) const
  {
    return (!(*reg(I2S_STATE_REG(_cfg.i2s_port)) & I2S_TX_IDLE));
  }

  void Bus_Parallel8::writeCommand(std::uint32_t data, std::uint_fast8_t bit_length)
  {
    bit_length = (bit_length + 7) >> 3;
    wait_i2s();
    if (sendmode != sendmode_t::sendmode_32bit_nodma)
    {
      sendmode = sendmode_t::sendmode_32bit_nodma;
      *_i2s_sample_rate_conf_reg = _sample_rate_conf_reg_32bit;
      *_i2s_fifo_conf_reg = _fifo_conf_default;
    }
    *_i2s_fifo_wr_reg = (data & 0xFF) << 16;
    while (--bit_length)
    {
      data >>= 8;
      *_i2s_fifo_wr_reg = (data & 0xFF) << 16;
    }
    *_i2s_conf_reg = _conf_reg_start;
  }

  void Bus_Parallel8::writeData(std::uint32_t data, std::uint_fast8_t bit_length)
  {
    bit_length = (bit_length + 7) >> 3;
    wait_i2s();
    if (sendmode != sendmode_t::sendmode_32bit_nodma)
    {
      sendmode = sendmode_t::sendmode_32bit_nodma;
      *_i2s_sample_rate_conf_reg = _sample_rate_conf_reg_32bit;
      *_i2s_fifo_conf_reg = _fifo_conf_default;
    }
    *_i2s_fifo_wr_reg = (0x100 | data) << 16;
    while (--bit_length)
    {
      data >>= 8;
      *_i2s_fifo_wr_reg = (0x100 | data) << 16;
    }
    *_i2s_conf_reg = _conf_reg_start;
  }

  void Bus_Parallel8::writeDataRepeat(std::uint32_t color_raw, std::uint_fast8_t bit_length, std::uint32_t length)
  {
    if (bit_length == 16)
    {
      auto conf_start = _conf_reg_start;
      std::uint32_t data = 0x01000100;
      data |= color_raw << 16 | color_raw >> 8;
      std::int32_t limit = ((length - 1) & 31) + 1;
      if (sendmode != sendmode_t::sendmode_16bit_nodma)
      {
        sendmode = sendmode_t::sendmode_16bit_nodma;
        wait_i2s();
        *_i2s_sample_rate_conf_reg = _sample_rate_conf_reg_16bit;
        *_i2s_fifo_conf_reg = _fifo_conf_default;
      }
//*
      for (;;)
      {
        length -= limit;
        wait_i2s();
        do
        {
          *_i2s_fifo_wr_reg = data;
        } while (--limit);
        *_i2s_conf_reg = conf_start;
        if (!length) return;
        limit = 32;
        std::uint32_t wait = 10;
        do { __asm__ __volatile__ ("nop"); } while (--wait && (*_i2s_state_reg & I2S_TX_IDLE));
      }
      if (!length) return;
//*/
//       auto i2s_int_clr_reg = reg(I2S_INT_CLR_REG(_cfg.i2s_port));
//       auto i2s_int_raw_reg = reg(I2S_INT_RAW_REG(_cfg.i2s_port));
//       auto i2s_int_st_reg = reg(I2S_INT_ST_REG(_cfg.i2s_port));
//       auto i2s_fifo_wr_reg = _i2s_fifo_wr_reg;
//       *i2s_int_clr_reg = I2S_TX_PUT_DATA_INT_RAW | I2S_TX_WFULL_INT_CLR;
//       bool sended = false;
// //*
//       wait_i2s();
//       limit = std::min(31u, length);
//       length -= limit;
//       do
//       {
//         *i2s_fifo_wr_reg = data;
//       } while (--limit);
//       *_i2s_conf_reg = conf_start;
//       if (!length) return;

//       std::uint32_t icount = 0;
//       std::uint32_t wait = 3;
//       do { __asm__ __volatile__ ("nop"); } while (--wait && (*_i2s_state_reg & I2S_TX_IDLE));
// //*/
//       for (;;)
//       {
// /*
//         if (*reg(I2S_STATE_REG(_cfg.i2s_port)) & I2S_TX_IDLE)
//         {
//           *_i2s_conf_reg = _conf_reg_default;
//           limit = std::min(31u, length);
//           length -= limit;
//           do
//           {
//             *i2s_fifo_wr_reg = data;
//           } while (--limit);
//           *_i2s_conf_reg = conf_start;
//           if (!length) return;
//           std::uint32_t i = 3;
//           do { __asm__ __volatile__ ("nop"); } while (--i);

//       //while (*reg(I2S_STATE_REG(_cfg.i2s_port)) & I2S_TX_IDLE);

//           //*_i2s_conf_reg = _conf_reg_default;
//           //sended = true;
//           //*_i2s_conf_reg = conf_start;
//         }
// //*/
//         if (!(*i2s_int_raw_reg & I2S_TX_WFULL_INT_ENA))
//         {
//           *i2s_fifo_wr_reg = data;
//           if (0 == --length) break;
// /*
// if (*_i2s_state_reg & I2S_TX_IDLE)
// {
//   ++icount;
//   if (icount > 32)
//   {
//     icount = 0;
//           *_i2s_conf_reg = _conf_reg_default;
//           *_i2s_conf_reg = conf_start;
//   }
// } else icount = 0;
// //*/
//         }
//         else
//         {
//           *i2s_int_clr_reg = I2S_TX_WFULL_INT_CLR;
//           *_i2s_conf_reg = _conf_reg_default;
//           *_i2s_conf_reg = conf_start;
//           icount = 0;
//           std::uint32_t wait = 32;
//           do { __asm__ __volatile__ ("nop"); } while (--wait && (*_i2s_state_reg & I2S_TX_IDLE));
// //while (!(*i2s_int_raw_reg & I2S_TX_PUT_DATA_INT_RAW));
//         }
// /*
//         if (!sended && !(intraw & I2S_TX_PUT_DATA_INT_ENA))
//         {
//           sended = true;
//           *i2s_int_clr_reg = I2S_TX_PUT_DATA_INT_ENA;
//           *_i2s_conf_reg = conf_start;
//         }
// //*/
//       }
//       //ets_delay_us(40);
//       if (*reg(I2S_STATE_REG(_cfg.i2s_port)) & I2S_TX_IDLE)
// //      if (!sended)
//         *_i2s_conf_reg = conf_start;
//       return;
    }
    else
    {
      if (length & 1)
      {
        writeData(color_raw, bit_length);
        if (!--length) return;
      }
      auto conf_start = _conf_reg_start;
      static constexpr std::uint32_t data_wr = 0x01000100;
      std::uint32_t data0 = color_raw << 16 | color_raw >>  8 | data_wr;
      std::uint32_t data1 = color_raw                         | data_wr;
      std::uint32_t data2 = color_raw <<  8 | color_raw >> 16 | data_wr;
      length >>= 1;
      std::uint32_t limit = ((length - 1) % 10) + 1;
      if (sendmode != sendmode_t::sendmode_16bit_nodma)
      {
        sendmode = sendmode_t::sendmode_16bit_nodma;
        wait_i2s();
        *_i2s_sample_rate_conf_reg = _sample_rate_conf_reg_16bit;
        *_i2s_fifo_conf_reg = _fifo_conf_default;
      }
      for (;;)
      {
        length -= limit;

        wait_i2s();
        do
        {
          *_i2s_fifo_wr_reg = data0;
          *_i2s_fifo_wr_reg = data1;
          *_i2s_fifo_wr_reg = data2;
        } while (--limit);
        *_i2s_conf_reg = conf_start;
        if (!length) return;
        limit = 10;
        std::uint32_t wait = 10;
        do { __asm__ __volatile__ ("nop"); } while (--wait && (*_i2s_state_reg & I2S_TX_IDLE));
      }
    }
  }

  void Bus_Parallel8::writePixels(pixelcopy_t* param, std::uint32_t length)
  {
    std::uint8_t buf[512];
    const std::uint32_t bytes = param->dst_bits >> 3;
    auto fp_copy = param->fp_copy;
    const std::uint32_t limit = (bytes == 2) ? 256 : 170;
    std::uint8_t len = length % limit;
    if (len) {
      fp_copy(buf, 0, len, param);
      writeBytes(buf, len * bytes, false);
      if (0 == (length -= len)) return;
    }
    do {
      fp_copy(buf, 0, limit, param);
      writeBytes(buf, limit * bytes, false);
    } while (length -= limit);
  }

  void Bus_Parallel8::writeBytes(const std::uint8_t* data, std::uint32_t length, bool use_dma)
  {
    auto conf_start = _conf_reg_start;
    static constexpr std::uint32_t data_wr = 0x01000100;

    if (length & 1) {
      writeData(data[0], 8);
      if (!--length) return;
      ++data;
    }

    if (sendmode != sendmode_t::sendmode_16bit_nodma)
    {
      sendmode = sendmode_t::sendmode_16bit_nodma;
      while (!(*_i2s_state_reg & I2S_TX_IDLE));
      *_i2s_sample_rate_conf_reg = _sample_rate_conf_reg_16bit;
      *_i2s_fifo_conf_reg = _fifo_conf_default;
    }

    auto conf_reg_stop = _conf_reg_default | I2S_TX_RESET | I2S_RX_RESET | I2S_RX_FIFO_RESET;

    auto i2s_fifo_wr_reg = _i2s_fifo_wr_reg;
    auto i2s_conf_reg = _i2s_conf_reg;
    do
    {
      std::int32_t limit = (((length>>1)-1)&(31))+1;
      length -= limit << 1;
      while (!(*_i2s_state_reg & I2S_TX_IDLE));
      *i2s_conf_reg = conf_reg_stop;
      do {
        *i2s_fifo_wr_reg = data[0] << 16 | data[1] | data_wr;
        data += 2;
      } while (--limit);
      *i2s_conf_reg = conf_start;
    /// WiFi,BT使用時はI2SのDMAを使用すると誤動作するため、DMA不使用での処理を継続する。
    } while (length && (*reg(DPORT_WIFI_CLK_EN_REG) & 0x7FF));
    if (!length) return;

// ここからDMA使用ルーチン

    sendmode = sendmode_t::sendmode_16bit_dma;
    std::uint32_t lbase = 64;
    auto i2s_out_link_reg = reg(I2S_OUT_LINK_REG(_cfg.i2s_port));
    do {
      auto buf = (std::uint32_t*)_flip_buffer.getBuffer(512);
      std::uint32_t limit = ((length - 1) & (lbase - 1)) + 1;
      length -= limit;
      _dmadesc->buf = (std::uint8_t*)buf;
      std::uint32_t i = 0;
      limit>>=1;
      do {
        buf[i] = data[0]<<16 | data[1] | data_wr;
        data += 2;
      } while (++i != limit);
      *(std::uint32_t*)_dmadesc = (((i<<2) + 3) &  ~3 ) | i << 14 | 0xC0000000;

      while (!(*_i2s_state_reg & I2S_TX_IDLE));
      *i2s_out_link_reg = I2S_OUTLINK_START | ((uint32_t)_dmadesc & I2S_OUTLINK_ADDR);
      *_i2s_conf_reg = conf_reg_stop;
      *_i2s_fifo_conf_reg = _fifo_conf_dma;

      // DMAの準備が完了するまでnopループで時間稼ぎ
      i = 11;
      do { __asm__ __volatile__ ("nop"); } while (--i);

      *_i2s_conf_reg = conf_start;
      if (lbase != 256) lbase <<= 1;
    } while (length);
  }

  std::uint_fast8_t Bus_Parallel8::_reg_to_value(std::uint32_t raw_value)
  {
    return ((raw_value >> _cfg.pin_d7) & 1) << 7
         | ((raw_value >> _cfg.pin_d6) & 1) << 6
         | ((raw_value >> _cfg.pin_d5) & 1) << 5
         | ((raw_value >> _cfg.pin_d4) & 1) << 4
         | ((raw_value >> _cfg.pin_d3) & 1) << 3
         | ((raw_value >> _cfg.pin_d2) & 1) << 2
         | ((raw_value >> _cfg.pin_d1) & 1) << 1
         | ((raw_value >> _cfg.pin_d0) & 1) ;
  }

  void Bus_Parallel8::beginRead(void)
  {
      wait();
      gpio_lo(_cfg.pin_rd);
//      gpio_pad_select_gpio(_gpio_rd);
//      gpio_set_direction(_gpio_rd, GPIO_MODE_OUTPUT);
      gpio_pad_select_gpio(_cfg.pin_wr);
      gpio_hi(_cfg.pin_wr);
      gpio_set_direction((gpio_num_t)_cfg.pin_wr, GPIO_MODE_OUTPUT);
      gpio_pad_select_gpio(_cfg.pin_rs);
      gpio_hi(_cfg.pin_rs);
      gpio_set_direction((gpio_num_t)_cfg.pin_rs, GPIO_MODE_OUTPUT);
//      if (_i2s_port == I2S_NUM_0) {
////        gpio_matrix_out(_gpio_rd, I2S0O_WS_OUT_IDX    ,1,0);
//        gpio_matrix_out(_gpio_rd, I2S0O_BCK_OUT_IDX    ,1,0);
//      } else {
////        gpio_matrix_out(_gpio_rd, I2S1O_WS_OUT_IDX    ,1,0);
//        gpio_matrix_out(_gpio_rd, I2S1O_BCK_OUT_IDX    ,1,0);
//      }
//*
//      auto idx_base = (_i2s_port == I2S_NUM_0) ? I2S0O_DATA_OUT8_IDX : I2S1O_DATA_OUT8_IDX;
//      gpio_matrix_in(_gpio_d7, idx_base + 7, 0); // MSB
//      gpio_matrix_in(_gpio_d6, idx_base + 6, 0);
//      gpio_matrix_in(_gpio_d5, idx_base + 5, 0);
//      gpio_matrix_in(_gpio_d4, idx_base + 4, 0);
//      gpio_matrix_in(_gpio_d3, idx_base + 3, 0);
//      gpio_matrix_in(_gpio_d2, idx_base + 2, 0);
//      gpio_matrix_in(_gpio_d1, idx_base + 1, 0);
//      gpio_matrix_in(_gpio_d0, idx_base    , 0); // LSB
//*/
/*
      gpio_pad_select_gpio(_gpio_d7); gpio_set_direction(_gpio_d7, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d6); gpio_set_direction(_gpio_d6, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d5); gpio_set_direction(_gpio_d5, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d4); gpio_set_direction(_gpio_d4, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d3); gpio_set_direction(_gpio_d3, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d2); gpio_set_direction(_gpio_d2, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d1); gpio_set_direction(_gpio_d1, GPIO_MODE_INPUT);
      gpio_pad_select_gpio(_gpio_d0); gpio_set_direction(_gpio_d0, GPIO_MODE_INPUT);
      set_clock_read();
/*/
      gpio_matrix_out(_cfg.pin_d7, 0x100, 0, 0); // MSB
      gpio_matrix_out(_cfg.pin_d6, 0x100, 0, 0);
      gpio_matrix_out(_cfg.pin_d5, 0x100, 0, 0);
      gpio_matrix_out(_cfg.pin_d4, 0x100, 0, 0);
      gpio_matrix_out(_cfg.pin_d3, 0x100, 0, 0);
      gpio_matrix_out(_cfg.pin_d2, 0x100, 0, 0);
      gpio_matrix_out(_cfg.pin_d1, 0x100, 0, 0);
      gpio_matrix_out(_cfg.pin_d0, 0x100, 0, 0); // LSB

      lgfx::pinMode(_cfg.pin_d7, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d6, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d5, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d4, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d3, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d2, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d1, pin_mode_t::input);
      lgfx::pinMode(_cfg.pin_d0, pin_mode_t::input);
//*/
  }

  void Bus_Parallel8::endRead(void)
  {
    wait();
    _init_pin();
  }

  std::uint32_t Bus_Parallel8::readData(std::uint_fast8_t bit_length)
  {
    union {
      std::uint32_t res;
      std::uint8_t raw[4];
    };
    bit_length = (bit_length + 7) & ~7;

    auto buf = raw;
    do {
      std::uint32_t tmp = GPIO.in;   // dummy read speed tweak.
      tmp = GPIO.in;
      gpio_hi(_cfg.pin_rd);
      gpio_lo(_cfg.pin_rd);
      *buf++ = _reg_to_value(tmp);
    } while (bit_length -= 8);
    return res;
  }

  void Bus_Parallel8::readBytes(std::uint8_t* dst, std::uint32_t length, bool use_dma)
  {
    do {
      std::uint32_t tmp = GPIO.in;   // dummy read speed tweak.
      tmp = GPIO.in;
      gpio_hi(_cfg.pin_rd);
      gpio_lo(_cfg.pin_rd);
      *dst++ = _reg_to_value(tmp);
    } while (--length);
  }

  void Bus_Parallel8::readPixels(void* dst, pixelcopy_t* param, std::uint32_t length)
  {
    std::uint32_t _regbuf[8];
    const auto bytes = param->dst_bits >> 3;
    std::uint32_t limit = (bytes == 2) ? 16 : 10;
    param->src_data = _regbuf;
    std::int32_t dstindex = 0;
    do {
      std::uint32_t len2 = (limit > length) ? length : limit;
      length -= len2;
      std::uint32_t i = len2 * bytes;
      auto d = (std::uint8_t*)_regbuf;
      do {
        std::uint32_t tmp = GPIO.in;
        gpio_hi(_cfg.pin_rd);
        gpio_lo(_cfg.pin_rd);
        *d++ = _reg_to_value(tmp);
      } while (--i);
      param->src_x = 0;
      dstindex = param->fp_copy(dst, dstindex, dstindex + len2, param);
    } while (length);
  }

//----------------------------------------------------------------------------
 }
}

#endif