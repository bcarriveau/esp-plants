ESP PLANTS -- PC transfer from Waveshare 7B to Waveshare 7
Based on waveshare-zigbee HEAD 093af9eab319aa24e24c6fce03cc1b1cf3c1737c

1. Unzip into your esp-plants repository so the script is under tools\.
2. Connect both Waveshare displays by USB to the Windows PC. Check Device Manager for actual COM ports.
3. Close VS Code Serial Monitor, PlatformIO Monitor and any program using those ports.
4. In PowerShell, from the repository root:
   powershell -ExecutionPolicy Bypass -File .\tools\transfer-waveshare-userdata.ps1

   Defaults: 7B on COM13 = source; original 7 on COM11 = target.
   If ports differ:
   powershell -ExecutionPolicy Bypass -File .\tools\transfer-waveshare-userdata.ps1 -SourcePort COM13 -TargetPort COM11

5. Script backs up BOTH plantdata partitions, then asks you to type TRANSFER before writing.
6. It reads back and SHA-256 checks the target. No firmware or H2 image is copied.
7. Saved backups are under Documents\ESP-PLANTS-Backups\transfer-YYYYMMDD-HHMMSS\.
   These contain saved Wi-Fi passwords. Do not publish or commit them to GitHub.
8. The Zigbee network resides on the separate ESP32-H2. To preserve pairings, move
   the SAME H2 unit from the 7B to the 7. The script does not clone the H2.

Read/backup only (no target write):
   powershell -ExecutionPolicy Bypass -File .\tools\transfer-waveshare-userdata.ps1 -SkipWrite

CAUTION: This assumes both displays run firmware with the SAME partition layout:
  plantdata data/nvs offset 0xC10000 size 0x20000.
Never run on a board with a different partition table. Do not restore full flash
or overwrite application/bootloader partitions. Flash encryption may prevent
raw transfer; stop if either device has flash encryption enabled.

After migrating, you may want to separately reset the 7B's settings to turn it
back into a test bench. This tool intentionally does NOT erase or change the 7B.

Scope: new tools/transfer-waveshare-userdata.ps1 only; zero firmware changes.
Suggested commit message: tools: add backed-up Waveshare 7B-to-7 userdata transfer
