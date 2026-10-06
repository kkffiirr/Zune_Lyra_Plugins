# Retry the USB launch of the Lyra installer (which uninstalls Lyra when it is installed) until it gets through.
$u = "C:\Program Files\usbipd-win\usbipd.exe"
$cmd = "cd /opt/lyra/zune-deploy/src/ZuneDeploy.CLI && timeout 240 dotnet run --no-build -- deploy --launch /opt/lyra/deploykit 2>&1 | tr '\r' '\n' | sed 's/\x1b\[[0-9;]*m//g' | grep -E 'Launched|Non OK|Failed|ErrorReadFailed|Deployed Content.nativeapp' | sort -u | head -6"
for ($i = 1; $i -le 40; $i++) {
    & $u attach --wsl --busid 7-2 2>&1 | Out-Null
    $o = wsl -d Ubuntu -u root -- bash -c $cmd
    "{0} try {1}: {2}" -f (Get-Date -f HH:mm:ss), $i, ($o -join ' | ')
    if ($o -match 'Launched' -or $o -match 'Non OK' -or $o -match 'ErrorReadFailed') { "SUCCESS-ish: launched"; break }
    Start-Sleep 5
}
