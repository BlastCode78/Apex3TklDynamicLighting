$ErrorActionPreference = 'Stop'

$instanceId = 'HID\VID_1038&PID_1622&MI_01\B&E382AE4&0&0000'

$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Apri PowerShell come amministratore e rilancia questo script.'
}

$packages = @(Get-WindowsDriver -Online | Where-Object {
    $_.ProviderName -eq 'Apex3TklDynamicLighting' -and
    $_.OriginalFileName -like '*apex3tkldynamiclighting.inf'
})

if ($packages.Count -eq 0) {
    Write-Output 'Il pacchetto Apex3TklDynamicLighting non e presente nel Driver Store.'
} else {
    foreach ($package in $packages) {
        Write-Output "Rimuovo il pacchetto del progetto $($package.Driver)..."
        pnputil /delete-driver $package.Driver /uninstall
        if ($LASTEXITCODE -ne 0) { throw "Impossibile rimuovere $($package.Driver); codice $LASTEXITCODE" }
    }
}

Write-Output "`nRilevo di nuovo l interfaccia HID..."
pnputil /scan-devices
if ($LASTEXITCODE -ne 0) { throw "pnputil /scan-devices ha restituito $LASTEXITCODE" }
Start-Sleep -Seconds 3

$device = Get-PnpDevice -PresentOnly | Where-Object InstanceId -EQ $instanceId
$device | Format-List Status, Class, FriendlyName, InstanceId
if (-not $device -or $device.Status -ne 'OK') {
    throw 'La MI_01 non e tornata OK. Riavvia il PC e controlla nuovamente lo stato del dispositivo.'
}

$deviceStack = pnputil /enum-devices /instanceid $instanceId /stack /drivers
$deviceStack
if ($deviceStack -match 'Nome driver:\s+oem\d+\.inf') {
    Write-Warning 'PnP mostra ancora un pacchetto oem*.inf attivo. Controlla l output dello stack prima di considerare concluso il ripristino.'
}

Write-Output "`nRipristino completato: la MI_01 e OK."
