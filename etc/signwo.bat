signtool.exe sign /s WinOFCertStore /n WinOFCert /t http://timestamp.verisign.com/scripts/timestamp.dll *.sys
signtool.exe sign /s WinOFCertStore /n WinOFCert /t http://timestamp.verisign.com/scripts/timestamp.dll *.dll
signtool.exe sign /s WinOFCertStore /n WinOFCert /t http://timestamp.verisign.com/scripts/timestamp.dll *.exe
signtool.exe sign /s WinOFCertStore /n WinOFCert /t http://timestamp.verisign.com/scripts/timestamp.dll *.cat

signtool.exe verify /pa /v *.cat
