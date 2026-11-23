# Run as administrator: shares the game files with the NES Mini over hakchi's
# USB network link (SMB 2.1, read/write so saves work).
$path = 'C:\MKW-3DS-Builder\build\release\sd-card'
$user = "$env:COMPUTERNAME\$env:USERNAME"
if (-not (Get-SmbShare -Name sd-card -ErrorAction SilentlyContinue)) {
    New-SmbShare -Name sd-card -Path $path -FullAccess $user | Out-Null
}
# hakchi's link is usually "Public", where Windows blocks file sharing.
Get-NetConnectionProfile | Where-Object { $_.IPv4Connectivity -ne 'Internet' -and $_.NetworkCategory -eq 'Public' } |
    Where-Object { (Get-NetIPAddress -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue).IPAddress -like '169.254.*' } |
    Set-NetConnectionProfile -NetworkCategory Private
Enable-NetFirewallRule -DisplayGroup 'File and Printer Sharing' -ErrorAction SilentlyContinue
Get-SmbShare -Name sd-card | Format-List Name, Path
