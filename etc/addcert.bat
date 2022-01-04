makecert -$ individual -r -pe -ss WinOFCertStore -n CN=WinOFCert WinOFCert.cer
clusrun certutil -addstore TrustedPublisher %LOGONSERVER%\c$\winof\install\WinOFCert.cer
clusrun certutil -addstore Root %LOGONSERVER%\c$\winof\install\WinOFCert.cer