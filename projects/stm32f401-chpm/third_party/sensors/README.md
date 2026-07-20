# sensors

`sensors` 是不依赖 STM32、HAL、ThreadX 或具体引脚的纯 C 传感器驱动包。

当前模块：

- `aht20`：通过 `sensor_i2c_bus_t` 注入完整 I2C 读写事务。
- `ds18b20`：通过 `sensor_onewire_bus_t` 注入 reset/read/write 操作，保留
  Skip ROM、Scratchpad、Dallas CRC8 和温度换算。

库不负责 RTOS 调度、转换等待、报警阈值、DWIN 或 Modbus 更新。调用方必须
使用静态或零初始化的设备上下文，并保证同一总线事务由一个 owner 串行调用。

主机验证：

```powershell
cmake -S . -B build -DSENSORS_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
