@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2017\WDExpress\Common7\Tools\VsDevCmd.bat"
msbuild IocpServer.vcxproj /p:configuration=release;platform=x64
pause
rem https://visualstudio.microsoft.com/ja/thank-you-downloading-visual-studio/?sku=Community&channel=Release&version=VS2022&source=VSLandingPage&cid=2030&passive=false
rem VisualStudioSetup.exe --channelUri https://aka.ms/vs/15/release/channel --productId Microsoft.VisualStudio.Product.WDExpress
