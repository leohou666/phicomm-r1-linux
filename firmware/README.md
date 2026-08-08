# Firmware

此目录用于本地保存从原厂系统提取的固件，例如：

- CYW43455 Wi-Fi firmware；
- board-specific NVRAM；
- CLM blob；
- Bluetooth HCD patch；
- AK7755 PRAM/CRAM 数据。

默认不要把这些二进制文件提交到公开仓库。建议在 `.gitignore` 中忽略，并只保存文件名、来源、版本、哈希和提取步骤。
