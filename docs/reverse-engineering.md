# 原厂系统逆向清单

## 1. Wi-Fi

搜索：

```sh
find /system /vendor /odm /etc /lib -type f \
  \( -iname '*43455*' -o -iname '*brcm*' -o -iname '*nvram*' \
     -o -iname '*.clm_blob' -o -iname 'fw_bcmdhd.bin' \) 2>/dev/null
```

记录：

- firmware 文件；
- NVRAM 文件；
- CLM blob；
- MAC 地址保存方式；
- WL_REG_ON GPIO；
- HOST_WAKE IRQ；
- regulator 和时钟。

## 2. Bluetooth

搜索：

```sh
find /system /vendor /odm /etc /lib -type f \
  \( -iname '*.hcd' -o -iname '*4345*' -o -iname '*bluetooth*' \) 2>/dev/null
```

记录：

- UART 编号；
- 初始和运行波特率；
- RTS/CTS；
- BT_REG_ON；
- HOST_WAKE / DEV_WAKE；
- HCD 文件；
- MAC 地址保存方式。

## 3. AK7755

搜索：

```sh
find /system /vendor /odm /etc /lib -type f \
  \( -iname '*7755*' -o -iname '*akm*' -o -iname '*dsp*' \
     -o -iname '*.bin' -o -iname '*.fw' \) 2>/dev/null
```

日志：

```sh
dmesg | grep -iE 'ak7755|akm|codec|dsp|i2s|audio|asoc'
```

内核字符串：

```sh
strings kernel | grep -iE 'ak7755|pram|cram|acram|ofreg'
```

如果固件没有以文件形式存在，应检查原厂驱动中是否包含静态数组。

## 4. ALSA 配置

搜索：

```sh
find /system /vendor /odm /etc -type f \
  \( -iname '*mixer*' -o -iname '*audio*policy*' -o -iname '*.conf' \) 2>/dev/null
```

重点记录：

- mixer_paths；
- PCM device 编号；
- I2S master/slave；
- slot 数和 slot width；
- 声道映射；
- 功放 enable/mute GPIO；
- 默认音量和增益。

## 5. 设备树

将 DTB 反编译：

```sh
dtc -I dtb -O dts -o r1-original.dts r1-original.dtb
```

优先搜索：

```sh
grep -niE 'sdio|wifi|wlan|bluetooth|uart|ak7755|i2s|sound|codec|amplifier' r1-original.dts
```
