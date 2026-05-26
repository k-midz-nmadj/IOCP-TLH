@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\WDExpress\Common7\Tools\VsDevCmd.bat"
:BUILD
msbuild IocpServer.vcxproj /p:configuration=release;platform=x64
set yn=
set /p yn="Rebuild?(y/n)"
if not "%yn%"=="n" goto :BUILD

rem https://visualstudio.microsoft.com/ja/thank-you-downloading-visual-studio/?sku=Community&channel=Release&version=VS2022&source=VSLandingPage&cid=2030&passive=false
rem VisualStudioSetup.exe --channelUri https://aka.ms/vs/15/release/channel --productId Microsoft.VisualStudio.Product.WDExpress
