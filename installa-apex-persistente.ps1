$ErrorActionPreference = 'Stop'

$instanceId = 'HID\VID_1038&PID_1622&MI_01\B&E382AE4&0&0000'
$infPath = Join-Path $PSScriptRoot 'x64\Debug\Apex3TklDynamicLighting\Apex3TklDynamicLighting.inf'
$publishedDriver = $null

try {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Apri PowerShell come amministratore e rilancia questo script.'
    }

    $testSigning = bcdedit /enum '{current}' | Select-String -Pattern 'testsigning\s+Yes'
    if (-not $testSigning) { throw 'Test Mode non risulta attivo; non installo il driver.' }
    if (-not (Test-Path -LiteralPath $infPath)) { throw "Pacchetto compilato non trovato: $infPath. Ricompila Debug x64 in Visual Studio." }

    $device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -EQ $instanceId
    if (-not $device -or $device.Status -ne 'OK') {
        throw 'La MI_01 non e OK prima dell installazione. Ripristina prima il driver Microsoft.'
    }

    Write-Output 'Test Mode attivo. Installo il filtro Apex 3 TKL e lo lascio attivo.'
    $addResult = pnputil /add-driver $infPath /install
    $addResult
    if ($LASTEXITCODE -ne 0) { throw "pnputil /add-driver ha restituito $LASTEXITCODE" }

    $publishedDriver = $addResult | ForEach-Object {
        [regex]::Match([string]$_, 'oem\d+\.inf', [System.Text.RegularExpressions.RegexOptions]::IgnoreCase).Value
    } | Where-Object { $_ } | Select-Object -First 1

    Write-Output "`nRiavvio dell interfaccia HID e verifica dello stack..."
    pnputil /restart-device $instanceId
    if ($LASTEXITCODE -ne 0) { throw "pnputil /restart-device ha restituito $LASTEXITCODE" }
    Start-Sleep -Seconds 4

    $device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -EQ $instanceId
    $device | Format-List Status, Class, FriendlyName, InstanceId
    $stack = pnputil /enum-devices /instanceid $instanceId /stack /drivers
    $stack

    if (-not $device -or $device.Status -ne 'OK' -or -not ($stack -match 'Apex3TklDynamicLighting')) {
        throw 'Il filtro non risulta avviato correttamente.'
    }

    $vhf = @(Get-PnpDevice -PresentOnly | Where-Object {
        $_.InstanceId -like '*HID_DEVICE_SYSTEM_VHF*' -and $_.Status -eq 'OK'
    })
    $vhf | Format-List Status, Class, FriendlyName, InstanceId
    if ($vhf.Count -eq 0) { throw 'Il filtro e attivo ma il dispositivo LampArray VHF non risulta presente.' }

    Write-Output "`nInstallazione riuscita. Filtro attivo; pacchetto pubblicato: $publishedDriver"
    Write-Output 'Per ripristinare il driver Microsoft, esegui ripristina-apex-microsoft.ps1 come amministratore.'
}
catch {
    Write-Output "Errore: $($_.Exception.Message)"
    if (-not $publishedDriver) {
        $package = Get-WindowsDriver -Online | Where-Object {
            $_.ProviderName -eq 'Apex3TklDynamicLighting' -and
            $_.OriginalFileName -like '*apex3tkldynamiclighting.inf'
        } | Sort-Object DriverDate -Descending | Select-Object -First 1
        if ($package) { $publishedDriver = $package.Driver }
    }
    if ($publishedDriver) {
        Write-Output "`nVerifica non riuscita: rimuovo il pacchetto $publishedDriver e ripristino il driver Microsoft..."
        pnputil /delete-driver $publishedDriver /uninstall
        pnputil /scan-devices
        Start-Sleep -Seconds 3
        Get-PnpDevice -PresentOnly | Where-Object InstanceId -EQ $instanceId |
            Format-List Status, Class, FriendlyName, InstanceId
    } else {
        Write-Output "`nNessun pacchetto installato del progetto da rimuovere."
    }
    exit 1
}
