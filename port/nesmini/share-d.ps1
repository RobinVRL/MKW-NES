# Run as administrator: shares the whole D: drive with the NES Mini over
# hakchi's USB network link as \\PC\D (read/write, so saves work).
$user = "$env:COMPUTERNAME\$env:USERNAME"
if (-not (Get-SmbShare -Name D -ErrorAction SilentlyContinue)) {
    New-SmbShare -Name D -Path 'D:\' -FullAccess $user | Out-Null
}
# hakchi's link is usually "Public", where Windows blocks file sharing.
Get-NetConnectionProfile | Where-Object { $_.NetworkCategory -eq 'Public' } |
    Where-Object { (Get-NetIPAddress -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue).IPAddress -like '169.254.*' } |
    Set-NetConnectionProfile -NetworkCategory Private
# The hakchi kernel's CIFS module only speaks SMB1 (needs the SMB1 server
# feature installed, plus a restart the first time).
Set-SmbServerConfiguration -EnableSMB1Protocol $true -Force -ErrorAction SilentlyContinue
Enable-NetFirewallRule -DisplayGroup 'File and Printer Sharing' -ErrorAction SilentlyContinue
Get-NetIPAddress -AddressFamily IPv4 | Where-Object IPAddress -like '169.254.*' | Format-Table InterfaceAlias, IPAddress
