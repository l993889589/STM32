$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$projectPath = Join-Path $root 'MDK-ARM\F4.uvprojx'
$checks = 0

function Assert-True([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    $script:checks++
}

[xml]$project = Get-Content -LiteralPath $projectPath
$target = $project.Project.Targets.Target
Assert-True ($target.TargetOption.TargetCommonOption.Device -eq 'STM32F401CCUx') 'Unexpected MCU target.'
Assert-True ($target.TargetOption.TargetCommonOption.CreateHexFile -eq '0') 'HEX generation must remain disabled.'

$groupNames = @($target.Groups.Group | ForEach-Object { [string]$_.GroupName })
Assert-True ($groupNames -contains 'ld_modbus') 'ld_modbus group is missing.'
Assert-True ($groupNames -contains 'ldc') 'LDC group is missing.'
Assert-True ($groupNames -contains 'dwin_protocol') 'dwin_protocol group is missing.'
Assert-True ($groupNames -contains 'sensors') 'Portable sensors group is missing.'
Assert-True ($groupNames -contains 'APP') 'Unified APP group is missing.'
Assert-True ($groupNames -contains 'BSP') 'Unified BSP group is missing.'
Assert-True (@($groupNames | Where-Object { $_ -cin 'drivers', 'services', 'shell', 'app', 'dwin' }).Count -eq 0) 'A superseded project-owned group remains active.'
Assert-True ($groupNames -notcontains 'libmodbus') 'Legacy libmodbus group is still active.'
Assert-True ($groupNames -notcontains 'osal') 'Unused one-RTOS OSAL group is still active.'

$appGroupText = (($target.Groups.Group | Where-Object { $_.GroupName -eq 'APP' }).Files.File.FilePath -join "`n")
$bspGroupText = (($target.Groups.Group | Where-Object { $_.GroupName -eq 'BSP' }).Files.File.FilePath -join "`n")
$dwinProtocolGroupText = (($target.Groups.Group | Where-Object { $_.GroupName -eq 'dwin_protocol' }).Files.File.FilePath -join "`n")
$sensorsGroupText = (($target.Groups.Group | Where-Object { $_.GroupName -eq 'sensors' }).Files.File.FilePath -join "`n")
Assert-True ($appGroupText -match 'user\\services\\app_services\.c') 'Services were not merged into APP.'
Assert-True ($appGroupText -match 'user\\app\\param\.c') 'Application sources were not merged into APP.'
Assert-True ($appGroupText -match 'user\\dwin\\dwin_app\.c') 'DWIN application sources were not merged into APP.'
Assert-True ($appGroupText -match 'user\\dwin\\dwin_tx\.c') 'The unique DWIN TX owner is not compiled in APP.'
Assert-True ($appGroupText -match 'user\\dwin\\dwin_tx_policy\.c') 'The DWIN scheduling policy is not compiled in APP.'
Assert-True ($dwinProtocolGroupText -match 'third_party\\dwin_protocol\\dwin_protocol\.c') 'The reusable DWIN encoder is not compiled.'
Assert-True ($dwinProtocolGroupText -match 'third_party\\dwin_protocol\\dwin_rx_parser\.c') 'The reusable DWIN stream parser is not compiled.'
Assert-True ($bspGroupText -match 'user\\bsp\\bsp\.c') 'Core BSP sources are missing from BSP.'
Assert-True ($bspGroupText -match 'user\\bsp\\bsp_onewire\.c') 'The PB0 1-Wire owner is not compiled in BSP.'
Assert-True ($bspGroupText -match 'user\\bsp\\bsp_sensor\.c') 'The board sensor adapters are not compiled in BSP.'
Assert-True ($sensorsGroupText -match 'third_party\\sensors\\src\\aht20\.c') 'The portable AHT20 driver is not compiled.'
Assert-True ($sensorsGroupText -match 'third_party\\sensors\\src\\ds18b20\.c') 'The portable DS18B20 driver is not compiled.'

$projectDir = Split-Path -Parent $projectPath
foreach ($file in $target.Groups.Group.Files.File) {
    $path = [string]$file.FilePath
    if ([string]::IsNullOrWhiteSpace($path)) { continue }
    $absolute = [IO.Path]::GetFullPath((Join-Path $projectDir $path))
    Assert-True (Test-Path -LiteralPath $absolute) "Project file is missing: $path"
}

$includes = [string]$target.TargetOption.TargetArmAds.Cads.VariousControls.IncludePath
Assert-True ($includes -notmatch 'libmodbus|FreeRTOS|STM32_USB_Device') 'Legacy middleware include path is active.'
Assert-True ($includes -match 'third_party/ld_modbus/include') 'ld_modbus include path is missing.'
Assert-True ($includes -match 'third_party/ldc') 'LDC 2.x include path is missing.'
Assert-True ($includes -match 'third_party/dwin_protocol') 'dwin_protocol include path is missing.'
Assert-True ($includes -match 'third_party/sensors/include') 'sensors include path is missing.'
Assert-True ($includes -notmatch 'ld_modbus/integrations/ldc|user/osal|user/(mid|shell|tlsf)') 'A removed middleware/include path is active.'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'user\mid'))) 'Unused user/mid directory still exists.'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'user\shell'))) 'Unused user/shell directory still exists.'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'user\tlsf'))) 'Unused user/tlsf directory still exists.'

$vscodeConfigPath = Join-Path $root '.vscode\c_cpp_properties.json'
$vscodeSettingsPath = Join-Path $root '.vscode\settings.json'
$workspacePath = Join-Path $root 'CHPM.code-workspace'
Assert-True (Test-Path -LiteralPath $vscodeConfigPath) 'VSCode C/C++ configuration is missing.'
Assert-True (Test-Path -LiteralPath $vscodeSettingsPath) 'VSCode workspace settings are missing.'
Assert-True (Test-Path -LiteralPath $workspacePath) 'CHPM.code-workspace is missing.'

$vscodeConfig = Get-Content -LiteralPath $vscodeConfigPath -Raw | ConvertFrom-Json
$vscodeTarget = $vscodeConfig.configurations | Select-Object -First 1
$vscodeSettings = Get-Content -LiteralPath $vscodeSettingsPath -Raw | ConvertFrom-Json
Assert-True (-not [string]::IsNullOrWhiteSpace([string]$vscodeTarget.compilerPath)) 'VSCode ARMClang compiler path is empty.'
Assert-True (Test-Path -LiteralPath ([string]$vscodeTarget.compilerPath)) 'VSCode ARMClang compiler path does not exist.'
Assert-True ($vscodeTarget.intelliSenseMode -eq 'windows-clang-arm') 'VSCode is not using ARMClang IntelliSense mode.'
Assert-True ($vscodeSettings.'clangd.enable' -eq $false) 'The duplicate clangd language server must stay disabled in this workspace.'

$vscodeDefines = @($vscodeTarget.defines | ForEach-Object { [string]$_ })
$keilDefines = @(([string]$target.TargetOption.TargetArmAds.Cads.VariousControls.Define) -split ',' |
    ForEach-Object { $_.Trim() } |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
foreach ($define in $keilDefines) {
    Assert-True ($vscodeDefines -contains $define) "VSCode is missing the Keil define: $define"
}

$vscodeIncludePaths = @($vscodeTarget.includePath | ForEach-Object {
    $expanded = ([string]$_).Replace('${workspaceFolder}', $root).Replace('/', '\')
    [IO.Path]::GetFullPath($expanded).TrimEnd('\')
})
foreach ($include in ($includes -split ';')) {
    if ([string]::IsNullOrWhiteSpace($include)) { continue }
    $absoluteInclude = [IO.Path]::GetFullPath((Join-Path $projectDir $include)).TrimEnd('\')
    Assert-True ($vscodeIncludePaths -contains $absoluteInclude) "VSCode is missing the Keil include path: $include"
}
foreach ($include in $vscodeIncludePaths) {
    Assert-True (Test-Path -LiteralPath $include) "VSCode include path does not exist: $include"
}

$projectText = Get-Content -LiteralPath $projectPath -Raw
Assert-True ($projectText -match 'ld_modbus_rtu_framer\.c') 'Strict ld_modbus RTU framer is not compiled.'
Assert-True ($projectText -match 'ld_modbus_client\.c') 'ld_modbus client implementation is not compiled.'
Assert-True ($projectText -match 'ld_modbus_server\.c') 'ld_modbus server implementation is not compiled.'
Assert-True ($projectText -match 'dwin_ldc_channel\.c') 'The single DWIN LDC owner is not compiled.'
Assert-True ($projectText -match 'dwin_tx\.c') 'The single DWIN transmit owner is not compiled.'
Assert-True ($projectText -match 'dwin_tx_policy\.c') 'The DWIN transmit policy is not compiled.'
Assert-True ($projectText -match 'dwin_rx_parser\.c') 'The DWIN length parser is not compiled.'
Assert-True ($projectText -match 'third_party\\ldc\\ldc\.c') 'Canonical LDC 2.x source is not compiled.'
Assert-True ($projectText -match 'stm32f4xx_hal_iwdg\.c') 'IWDG HAL driver is not compiled.'
Assert-True ($projectText -notmatch 'ld_modbus_ldc\.(c|h)|ldc_channels\.(c|h)|ldc_port_irq\.h') 'Superseded LDC integration remains in the Keil project.'

$excludedSourceRelativePaths = @(
    'user\app\app_pwm.c',
    'user\app\app_uart.c',
    'user\app\app_uart_recv.c',
    'user\services\bsp_soft_timer.c'
)
$excludedSources = @($excludedSourceRelativePaths | ForEach-Object {
    [IO.Path]::GetFullPath((Join-Path $root $_))
})
$intendedSources = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'user') -Recurse -File -Filter *.c |
        Where-Object { $excludedSources -notcontains $_.FullName }
    Get-ChildItem -LiteralPath (Join-Path $root 'third_party\ldc') -File -Filter *.c
    Get-ChildItem -LiteralPath (Join-Path $root 'third_party\dwin_protocol') -File -Filter *.c
    Get-ChildItem -LiteralPath (Join-Path $root 'third_party\sensors\src') -File -Filter *.c
    Get-ChildItem -LiteralPath (Join-Path $root 'third_party\ld_modbus\src') -File -Filter *.c
)
$compiledPaths = @($target.Groups.Group.Files.File | ForEach-Object {
    $path = [string]$_.FilePath
    if (-not [string]::IsNullOrWhiteSpace($path)) {
        [IO.Path]::GetFullPath((Join-Path $projectDir $path))
    }
})
foreach ($source in $intendedSources) {
    Assert-True ($compiledPaths -contains $source.FullName) "Intended source is not compiled: $($source.FullName)"
}
foreach ($source in $excludedSources) {
    Assert-True (Test-Path -LiteralPath $source) "Documented excluded source is missing: $source"
    Assert-True ($compiledPaths -notcontains $source) "Legacy/unused source unexpectedly entered the target: $source"
}

$ldcHeader = Get-Content -LiteralPath (Join-Path $root 'third_party\ldc\ldc.h') -Raw
Assert-True ($ldcHeader -match '#define\s+LDC_VERSION_MAJOR\s+2U') 'LDC major version is not 2.'
Assert-True ($ldcHeader -match '#define\s+LDC_VERSION_PATCH\s+2U') 'LDC patch version is not 2.0.2.'

$descriptor = Get-Content -LiteralPath (Join-Path $root 'USBX\App\ux_device_descriptors.h') -Raw
Assert-True ($descriptor -match '#define\s+USBD_VID\s+1155\b') 'USB VID differs from the legacy CDC device.'
Assert-True ($descriptor -match '#define\s+USBD_PID\s+22336\b') 'USB PID differs from 0x5740.'
Assert-True ($descriptor -match 'USBD_PRODUCT_STRING\s+"STM32 Virtual ComPort"') 'USB product string changed.'
Assert-True ($descriptor -match 'USBD_CDCACM_EPINCMD_ADDR\s+0x82U') 'CDC notification endpoint must be 0x82.'
Assert-True ($descriptor -match 'USBD_CDCACM_EPIN_ADDR\s+0x81U') 'CDC data IN endpoint must be 0x81.'
Assert-True ($descriptor -match 'USBD_CDCACM_EPOUT_ADDR\s+0x01U') 'CDC data OUT endpoint must be 0x01.'

$pwmSource = Get-Content -LiteralPath (Join-Path $root 'user\bsp\bsp_pwm.c') -Raw
$halConfig = Get-Content -LiteralPath (Join-Path $root 'Core\Inc\stm32f4xx_hal_conf.h') -Raw
Assert-True ($halConfig -match '(?m)^#define\s+HAL_IWDG_MODULE_ENABLED\b') 'IWDG HAL module is not enabled.'
Assert-True ($pwmSource -match '#define\s+BSP_PWM_INSTANCE\s+TIM1\b') 'Fan PWM must use TIM1.'
Assert-True ($pwmSource -match '#define\s+BSP_PWM_PIN\s+GPIO_PIN_8\b') 'Fan PWM must use PA8.'
Assert-True ($pwmSource -notmatch 'BSP_PWM_PIN\s+GPIO_PIN_6') 'PA6 must remain SPI1 MISO, not PWM.'

Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'Middlewares\Third_Party\FreeRTOS'))) 'FreeRTOS source tree remains in target.'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'Middlewares\ST\STM32_USB_Device_Library'))) 'Legacy USB Device stack remains in target.'

$activeSource = @(
    (Join-Path $root 'Core')
    (Join-Path $root 'user')
    (Join-Path $root 'third_party\dwin_protocol')
    (Join-Path $root 'third_party\sensors\include')
    (Join-Path $root 'third_party\sensors\src')
)
$legacyMatches = Get-ChildItem -LiteralPath $activeSource -Recurse -File |
    Where-Object { $_.Extension -in '.c', '.h' } |
    Select-String -Pattern '#include\s+["<](FreeRTOS|task|queue|event_groups|cmsis_os|usbd_|usb_device|modbus-rtu|modbus\.h|liqueue)' -CaseSensitive:$false
Assert-True (@($legacyMatches).Count -eq 0) 'Active source still includes a legacy RTOS/USB/Modbus/queue header.'

$integrationFiles = Get-ChildItem -LiteralPath $activeSource -Recurse -File |
    Where-Object {
        $_.Extension -in '.c', '.h'
    }
$integrationText = ($integrationFiles | ForEach-Object {
    Get-Content -LiteralPath $_.FullName -Raw
}) -join "`n"
Assert-True ($integrationText -notmatch 'debug_uart_queue|usb_uart_queue|msg_queue|modbus_queue') 'Mechanical legacy LDC channel APIs remain active.'
Assert-True ($integrationText -notmatch 'drv_aht20|drv_ds18b20') 'Superseded board-bound sensor drivers remain active.'
Assert-True ($integrationText -notmatch 'DEFINE_LDC_CHANNEL|ldc_easy_|ldc_core\.h') 'Legacy LDC 1.x adapter API remains active.'
Assert-True ($integrationText -match 'LDC_FULL_REJECT_NEW') 'DWIN does not reject new frames under backpressure.'
Assert-True ($integrationText -match 'result\.accepted_bytes\s*!=\s*length') 'DWIN admission does not verify transactional acceptance.'
Assert-True ($integrationText -match 'ldc_rx_idle\s*\(&dwin_queue\)') 'DWIN explicit IDLE boundary is missing.'
Assert-True ($integrationText -match '#define\s+DWIN_PROTOCOL_MAX_FRAME_BYTES\s+258U') 'DWIN LEN=255 frame capacity is not 258 bytes.'
Assert-True ($integrationText -match 'dwin_rx_parser_feed\s*\(') 'DWIN bytes are not framed by 5A A5 LEN.'
Assert-True ($integrationText -match 'dwin_rx_parser_on_idle\s*\(') 'DWIN incomplete IDLE recovery is missing.'
Assert-True ($integrationText -match 'DWIN_RX_CHUNK_COUNT\s+4U') 'DWIN ISR-to-owner chunk queue is not statically bounded.'
Assert-True ($integrationText -match 'dwin_ldc_channel_owner_wait\s*\(\s*app_timeout_ticks') 'DWIN single consumer wait is not health-bounded.'
Assert-True ($integrationText -match 'dwin_tx_policy_submit_latest\s*\(') 'DWIN latest-value coalescing is missing.'
Assert-True ($integrationText -match 'DWIN_TX_BUZZER_PERIOD_MS\s+5000U') 'Reliable five-second DWIN buzzer schedule is missing.'
Assert-True ($integrationText -notmatch 'dwin_ldc_channel_request_(begin|wait|end)|require_ack') 'Fixed DWIN ACKs are still treated as transaction identifiers.'
Assert-True ($integrationText -notmatch 'app_output_queue|enqueue_data\s*\(') 'Superseded DWIN output queue remains active.'
Assert-True ($integrationText -match 'drv_modbus_port_read_frame\s*\(') 'Modbus server is not consuming strict framed ADUs.'
Assert-True ($integrationText -match 'ld_modbus_server_process_rtu_adu\s*\(') 'Complete RTU frames are not passed directly to ld_modbus server.'
Assert-True ($integrationText -notmatch 'ld_modbus_ldc|app_modbus_dispatch') 'Second-level Modbus LDC adapter remains active.'
Assert-True ($integrationText -match 'usb_parse_crc\s*\(data,\s*\(int\)actual_length\)') 'USBX data is not delivered directly to its parser.'
Assert-True ($integrationText -notmatch '\b(Param_Store|ParamGet|ParamSet|ParamSetPlus|ParamStorePlus|ParamStoreEx|load_param|load_base_param|restroe_factory_param_setting|param_rs485_addr_set|param_fan_mode_set|param_pwm_get|param_pwm_set|param_pwm_auto_set|param_pwm_manual_set|update_disk_alarm_status|load_realy_state|get_temp)\s*\(') 'A legacy parameter compatibility bypass remains active.'

$paramSource = Get-Content -LiteralPath (Join-Path $root 'user\app\param.c') -Raw
$modbusPolicySource = Get-Content -LiteralPath (Join-Path $root 'user\app\app_modbus_param_policy.c') -Raw
$flashSource = Get-Content -LiteralPath (Join-Path $root 'user\drivers\drv_w25qxx.c') -Raw
$mainSource = Get-Content -LiteralPath (Join-Path $root 'Core\Src\main.c') -Raw
$sensorLibraryText = (Get-ChildItem -LiteralPath (Join-Path $root 'third_party\sensors\include'), (Join-Path $root 'third_party\sensors\src') -File |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
Assert-True ($sensorLibraryText -notmatch 'stm32|GPIO|HAL_|bsp_|tx_|ThreadX') 'The portable sensors library depends on STM32, BSP, or RTOS symbols.'
Assert-True ($sensorLibraryText -match 'SENSOR_STATUS_CRC_ERROR') 'DS18B20 CRC failures are not represented explicitly.'
Assert-True ($sensorLibraryText -match 'sensor_i2c_bus_t') 'AHT20 does not use an injected I2C dependency.'
Assert-True ($sensorLibraryText -match 'sensor_onewire_bus_t') 'DS18B20 does not use an injected 1-Wire dependency.'
Assert-True ($mainSource -match 'UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_READ_TIMEOUT') 'USB CDC read timeout is not applied to USBX.'
Assert-True ($mainSource -match 'UX_SLAVE_CLASS_CDC_ACM_IOCTL_SET_WRITE_TIMEOUT') 'USB CDC write timeout is not applied to USBX.'
Assert-True ($mainSource -match 'bsp_health_init\(APP_HEALTH_REQUIRED_SERVICES\)') 'The watchdog does not supervise the required services.'
Assert-True ($mainSource -notmatch '\(void\)tx_thread_create\s*\(') 'A ThreadX worker creation result is still ignored.'
Assert-True ($mainSource -match '(?s)store_status\s*=\s*ParamCommit.*acquire_mutex\(\);\s*ParamPublishRuntime\(\);\s*release_mutex\(\);') 'Durable parameters are not published under the shared-state lock.'
Assert-True ($paramSource -match '#define\s+PARAM_RECORD_STRIDE\s+64UL') 'Parameter persistence is not using the bounded append journal.'
Assert-True ($paramSource -match '(?s)param_store_candidate.*sf_verify.*sf_program\(commit_address.*sf_read.*g_tParam\s*=\s*\*candidate') 'Runtime parameters may be published before flash commit verification.'
Assert-True ($paramSource -match '(?s)ParamLoad.*param_scan_sector.*s_next_record_address\s*=\s*free_') 'Boot scanning does not cache the next journal address.'
Assert-True ($paramSource -match '(?s)param_store_candidate.*s_next_record_address.*PARAM_STORE_STATUS_SPARE_NOT_READY') 'Runtime commits may rescan or erase instead of using the cached journal position.'
Assert-True ($paramSource -match '(?s)ParamSpareNeedsErase.*ParamPrepareSpare.*sf_erase_sector_checked') 'Inactive journal-sector preparation is missing.'
Assert-True ($flashSource -match 'g_wait_hook\s*\(\s*\)') 'W25Q64 busy polling does not yield through the application hook.'
Assert-True ($flashSource -match '(?s)sf_erase_sector_checked.*sf_wait_ready\(SF_SECTOR_TIMEOUT_MS\).*sf_verify_erased_sector') 'W25Q64 sector erase is not verified by read-back.'
Assert-True ($flashSource -match '#define\s+SF_PAGE_TIMEOUT_MS\s+\(10U\)') 'W25Q64 page-program timeout differs from the reviewed bound.'
Assert-True ($flashSource -match '#define\s+SF_SECTOR_TIMEOUT_MS\s+\(500U\)') 'W25Q64 sector-erase timeout differs from the reviewed bound.'
Assert-True ($mainSource -match '#define\s+APP_PARAM_STORE_PRIORITY\s+4U') 'The flash owner priority no longer matches the reviewed scheduling policy.'
Assert-True ($mainSource -match 'LD_MODBUS_EXCEPTION_SERVER_DEVICE_FAILURE') 'A failed Modbus parameter commit has no device-failure response.'
Assert-True ($mainSource -match '#define\s+APP_PARAM_RESPONSE_TIMEOUT_MS\s+\(100U\)') 'The Modbus parameter response wait is not bounded to 100 ms.'
Assert-True ($mainSource -match 'LD_MODBUS_EXCEPTION_ACKNOWLEDGE') 'A late accepted parameter commit has no Modbus acknowledge response.'
Assert-True ($mainSource -match 'LD_MODBUS_EXCEPTION_SERVER_DEVICE_BUSY') 'A busy flash owner has no Modbus busy response.'
Assert-True ($mainSource -match '(?s)app_param_store_entry.*ParamSpareNeedsErase.*APP_PARAM_STORE_STATE_PREPARING.*ParamPrepareSpare') 'Spare-sector erase is not owned by the idle flash thread.'
Assert-True ($mainSource -match 'ld_modbus_rtu_encode\(view\.unit_id') 'Late Modbus exceptions may use a changed unit address.'
Assert-True ($mainSource -match 'static app_param_submit_status_t app_param_store_submit_locked\(') 'The bounded parameter submission function is missing.'
Assert-True ($mainSource -notmatch '(?s)tx_event_flags_get\(\s*&app_param_store_events,\s*APP_PARAM_STORE_COMPLETE,\s*TX_OR_CLEAR,\s*&actual_flags,\s*TX_WAIT_FOREVER\s*\)') 'The Modbus parameter completion event still waits forever.'
Assert-True ($modbusPolicySource -match '(?s)manual_changed.*APP_MODBUS_FAN_OUTPUT_NONE') 'Automatic-mode manual preset writes may change the live fan output.'

$modbusPortText = Get-Content -LiteralPath (Join-Path $root 'user\services\drv_modbus_port.c') -Raw
$uartSource = Get-Content -LiteralPath (Join-Path $root 'user\bsp\bsp_uart.c') -Raw
Assert-True ($uartSource -match '#define\s+BSP_UART_RX_BUFFER_SIZE\s+\(256U\)') 'UART circular DMA buffer is not 256 bytes.'
Assert-True ($uartSource -match 'HAL_UART_RXEVENT_HT') 'UART half-transfer boundary monitoring is missing.'
Assert-True ($uartSource -match 'HAL_UART_RXEVENT_TC') 'UART full-transfer boundary monitoring is missing.'
Assert-True ($uartSource -match 'diagnostics\.rx_overflows\+\+') 'UART DMA discontinuities are not diagnosed.'
Assert-True ($modbusPortText -match '(?s)dma_position\s*=\s*bsp_uart_rx_dma_position\(BSP_UART_MODBUS\);\s*if\(dma_position\s*==\s*modbus_last_dma_position\)\s*\{\s*now_cycles\s*=\s*bsp_dwt_get_cycles\(\)') 'Modbus protocol time may advance before checking for undelivered DMA bytes.'
Assert-True ($modbusPortText -match '(?s)ld_modbus_rtu_framer_init\([^;]+MODBUS_BITS_PER_CHARACTER,\s*bsp_dwt_frequency_hz\(\)\)') 'Modbus framer is not configured for the BSP DWT cycle timebase.'
Assert-True ($modbusPortText -notmatch 'modbus_time_from_cycles|last_byte_us|t35_us') 'Obsolete microsecond conversion remains in the Modbus port.'

$applicationFiles = @(
    Get-Item -LiteralPath (Join-Path $root 'Core\Src\main.c')
    Get-ChildItem -LiteralPath (Join-Path $root 'user\app') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h' }
    Get-ChildItem -LiteralPath (Join-Path $root 'user\services') -Recurse -File |
        Where-Object {
            $_.Extension -in '.c', '.h' -and
            $_.Name -notin 'bsp_soft_timer.c', 'bsp_soft_timer.h'
        }
    Get-ChildItem -LiteralPath (Join-Path $root 'user\dwin') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h' }
    Get-ChildItem -LiteralPath (Join-Path $root 'user\ldc') -Recurse -File |
        Where-Object { $_.Extension -in '.c', '.h' }
)
$hardwareLeaks = $applicationFiles |
    Select-String -Pattern '\b(HAL_[A-Za-z0-9_]+|GPIO[A-I]|TIM[0-9]+|USART[0-9]+|DMA[12]_Stream[0-9]+|[A-Z]+_HandleTypeDef|DWT->|SystemCoreClock)\b'
Assert-True (@($hardwareLeaks).Count -eq 0) 'Application/protocol code still exposes an STM32 peripheral, HAL API, handle, or register.'

$dwinWriteCallers = Get-ChildItem -LiteralPath (Join-Path $root 'Core'), (Join-Path $root 'user') -Recurse -File |
    Where-Object { $_.Extension -eq '.c' -and $_.FullName -notlike '*\user\drivers\drv_dwin.c' } |
    Select-String -Pattern '\bdrv_dwin_write\s*\('
Assert-True (@($dwinWriteCallers).Count -eq 1) 'More than one project source calls the low-level DWIN writer.'
Assert-True ($dwinWriteCallers[0].Path -eq (Join-Path $root 'user\dwin\dwin_tx.c')) 'The low-level DWIN writer is not owned exclusively by dwin_tx.c.'

$scriptText = Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -File -Filter *.ps1 |
    Where-Object { $_.Name -ne 'validate_project.ps1' } |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }
Assert-True (($scriptText -join "`n") -notmatch 'STM32CubeProgrammer|JLinkExe|ST-LINK_CLI|UV4\.exe\s+-f') 'A script contains a programming/debug-probe action.'

Write-Host "Static validation passed: $checks checks."
