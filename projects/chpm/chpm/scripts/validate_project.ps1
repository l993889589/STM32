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
Assert-True ($groupNames -contains 'dwin') 'DWIN owner/request compatibility group is missing.'
Assert-True ($groupNames -notcontains 'libmodbus') 'Legacy libmodbus group is still active.'
Assert-True ($groupNames -notcontains 'osal') 'Unused one-RTOS OSAL group is still active.'

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
Assert-True ($includes -notmatch 'ld_modbus/integrations/ldc|user/osal') 'Removed LDC adapter/OSAL include path is active.'

$projectText = Get-Content -LiteralPath $projectPath -Raw
Assert-True ($projectText -match 'ld_modbus_rtu_framer\.c') 'Strict ld_modbus RTU framer is not compiled.'
Assert-True ($projectText -match 'dwin_ldc_channel\.c') 'The single DWIN LDC owner is not compiled.'
Assert-True ($projectText -match 'third_party\\ldc\\ldc\.c') 'Canonical LDC 2.x source is not compiled.'
Assert-True ($projectText -notmatch 'ld_modbus_ldc\.(c|h)|ldc_channels\.(c|h)|ldc_port_irq\.h') 'Superseded LDC integration remains in the Keil project.'

$intendedSources = @(
    Get-ChildItem -LiteralPath (Join-Path $root 'user\services') -File -Filter *.c
    Get-ChildItem -LiteralPath (Join-Path $root 'user\dwin') -File -Filter *.c
    Get-ChildItem -LiteralPath (Join-Path $root 'user\ldc') -Recurse -File -Filter *.c
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

$board = Get-Content -LiteralPath (Join-Path $root 'user\bsp\board_config.h') -Raw
Assert-True ($board -match 'BOARD_FAN_PWM_TIMER\s+TIM1\b') 'Fan PWM must use TIM1.'
Assert-True ($board -match 'BOARD_FAN_PWM_GPIO_PIN\s+GPIO_PIN_8\b') 'Fan PWM must use PA8.'
Assert-True ($board -notmatch 'BOARD_PWM.*GPIO_PIN_6') 'PA6 must remain SPI1 MISO, not PWM.'

Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'Middlewares\Third_Party\FreeRTOS'))) 'FreeRTOS source tree remains in target.'
Assert-True (-not (Test-Path -LiteralPath (Join-Path $root 'Middlewares\ST\STM32_USB_Device_Library'))) 'Legacy USB Device stack remains in target.'

$activeSource = @(
    (Join-Path $root 'Core')
    (Join-Path $root 'user')
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
Assert-True ($integrationText -notmatch 'DEFINE_LDC_CHANNEL|ldc_easy_|ldc_core\.h') 'Legacy LDC 1.x adapter API remains active.'
Assert-True ($integrationText -match 'LDC_FULL_REJECT_NEW') 'DWIN does not reject new frames under backpressure.'
Assert-True ($integrationText -match 'result\.accepted_bytes\s*!=\s*length') 'DWIN admission does not verify transactional acceptance.'
Assert-True ($integrationText -match 'ldc_rx_idle\s*\(&dwin_queue\)') 'DWIN explicit IDLE boundary is missing.'
Assert-True ($integrationText -match 'dwin_ldc_channel_owner_wait\s*\(TX_WAIT_FOREVER\)') 'DWIN single consumer owner thread is missing.'
Assert-True ($integrationText -match 'drv_modbus_port_read_frame\s*\(') 'Modbus server is not consuming strict framed ADUs.'
Assert-True ($integrationText -match 'ld_modbus_server_process_rtu_adu\s*\(') 'Complete RTU frames are not passed directly to ld_modbus server.'
Assert-True ($integrationText -notmatch 'ld_modbus_ldc|app_modbus_dispatch') 'Second-level Modbus LDC adapter remains active.'
Assert-True ($integrationText -match 'usb_parse_crc\s*\(data,\s*\(int\)actual_length\)') 'USBX data is not delivered directly to its parser.'

$modbusPortText = Get-Content -LiteralPath (Join-Path $root 'user\services\drv_modbus_port.c') -Raw
Assert-True ($modbusPortText -match '(?s)dma_position\s*=\s*bsp_uart_rx_dma_position\(BSP_UART_MODBUS\);\s*if\(dma_position\s*==\s*modbus_last_dma_position\)\s*\{\s*now_cycles\s*=\s*DWT->CYCCNT') 'Modbus protocol time may advance before checking for undelivered DMA bytes.'
Assert-True ($modbusPortText -match '(?s)ld_modbus_rtu_framer_init\([^;]+MODBUS_BITS_PER_CHARACTER,\s*SystemCoreClock\)') 'Modbus framer is not configured for the raw DWT cycle timebase.'
Assert-True ($modbusPortText -notmatch 'modbus_time_from_cycles|last_byte_us|t35_us') 'Obsolete microsecond conversion remains in the Modbus port.'

$scriptText = Get-ChildItem -LiteralPath (Join-Path $root 'scripts') -File -Filter *.ps1 |
    Where-Object { $_.Name -ne 'validate_project.ps1' } |
    ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }
Assert-True (($scriptText -join "`n") -notmatch 'STM32CubeProgrammer|JLinkExe|ST-LINK_CLI|UV4\.exe\s+-f') 'A script contains a programming/debug-probe action.'

Write-Host "Static validation passed: $checks checks."
