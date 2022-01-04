'/*
' * Copyright (c) 2012,2013 Intel Corporation.  All rights reserved.
' *
' * This software is available to you under the Open.org BSD license
' * below:
' *
' *     Redistribution and use in source and binary forms, with or
' *     without modification, are permitted provided that the following
' *     conditions are met:
' *
' *      - Redistributions of source code must retain the above
' *        copyright notice, this list of conditions and the following
' *        disclaimer.
' *
' *      - Redistributions in binary form must reproduce the above
' *        copyright notice, this list of conditions and the following
' *        disclaimer in the documentation and/or other materials
' *        provided with the distribution.
' *
' * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
' * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
' * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
' * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
' * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
' * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
' * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
' * SOFTWARE.
' */

' WIX CustomActions used in the OFED for Windows Release.
' File is based on the installer src contributed by Mellanox Technologies.
'
' TabStops == 4
'
' $Id$

' //msdn.microsoft.com/en-us/library/d5fk67ky(VS.85).aspx
Const WindowStyle		= 7

' VersionNT values
Const WindowsXP			="501"
Const WindowsSvr2003	="502"
Const WindowsVista		="600"
Const WindowsSvr2008	="600"
Const Windows7			="601"
Const Windows8			="602"

Const UseDPinst			= "501" ' use DPinst.exe to install drivers for
								' Windows VersionNT >= this value.

' Global debug flag: Session.Property from msiexec.exe cmd line DBG=1
Dim sDBG


' Write string to MSI log file

Function MsiLogInfo(msg)
    Dim rec
    Set rec = Session.Installer.CreateRecord(1) 
    rec.StringData(0) = msg
    MsiLogInfo = Session.Message(&H04000000, rec)
End Function


Sub ShowError()
    If Err.Number = 0 Then
        Exit Sub
    End if
	strMsg = vbCrLf & "Error # " & Err.Number & vbCrLf & _
	         Err.Description & vbCrLf & vbCrLf
	msgbox strMsg
	MsiLogInfo strMsg

End Sub


Sub ErrMsg(msg)
    If Err.Number <> 0 Then
	    msgbox msg & vbCrLf & "Err # " & Err.Number & vbCrLf & Err.Description
    	Err.clear
    End if
End Sub

Function ShowErr2(msg)
    If Err.Number <> 0 Then
	    strMsg = vbCrLf & "Err # " & Err.Number & vbCrLf & _
	             Err.Description & vbCrLf
	    msgbox msg & strMsg
    End if
    ShowErr2=Err.Number
End Function


Function Architecture()
      Dim Arch,item
      For Each item In GetObject("winmgmts:root/cimv2").ExecQuery("SELECT Architecture FROM Win32_Processor")
                  Arch=item.Architecture
                  Exit For
      Next
      
      If (Arch=0) Then
                  Arch="x86"
      Elseif (Arch=1) Then
                  Arch="MIPS"
      Elseif (Arch=2) Then
                  Arch="Alpha"
      Elseif (Arch=3) Then
                  Arch="PowerPC"
      Elseif (Arch=6) Then
                  Arch="ia64"
      Elseif (Arch=9) Then
                  'Arch="x64"
                  Arch="amd64"
      Else
                  WScript.echo "Arch ID=" & Arch
                  Arch="CustomAction.vbs: Unable to determine Architecture"
      End If
      Architecture=Arch

End Function


' A CustomAction (CA) that runs after SetupInitialize which sets up
' CustomAction Data for the defered action PostDriverInstall.
' A CA can only see Installer properties through pre-loaded 'CustomActionData'
' If ADDLOCAL is <null> case of 'msiexec /I foo.msi /passive' then set a
' default set of install features in ADDLOCAL.

Sub WinOF_setup
	dim VersionNT,Installed,AddLocal

	VersionNT = Session.Property("VersionNT")
	Installed = Session.Property("Installed")
	AddLocal = Session.Property("ADDLOCAL")

	' The WIX UI (UserInterface) sets up ADDLOCAL. When cmd-line msiexec.exe is
	' run with a minimal UI (/passive), then ADDLOCAL is not setup correctly;
	' default it's value here.

	If AddLocal = "" AND Installed = "" Then
		' Enable default features.
		AddLocal = "IBcore,fIPoIB,fDAPL,fDatBASIC1,fDatBASIC2,fND,fRsock" 
	End If

	If Session.Property("OSM") = "1" OR Session.Property("OSMS") = "1" Then
		AddLocal = AddLocal & ",fOSMS"
	End If

	If Session.Property("SRP") = "1" Then
		AddLocal = AddLocal & ",fSRP"
	End If

	If Session.Property("VNIC") = "1" Then
		AddLocal = AddLocal & ",fVNIC"
	End If

	' Driver Install Properties:
    ' 0-INSTALLDIR; 1-SystemFolder; 2-System64Folder; 3-WindowsFolder ;
	' 4-VersionNT; 5-ADDLOCAL; 6-REMOVE; 7-NODRV; 8-DBG

	Session.Property("PostDriverInstall") = _
		Session.Property("INSTALLDIR")		& ";" & _
		Session.Property("SystemFolder")	& ";" & _
		Session.Property("System64Folder")	& ";" & _
		Session.Property("WindowsFolder")	& ";" & _
		VersionNT							& ";" & _
		AddLocal							& ";" & _
		Session.Property("REMOVE")			& ";" & _
		Session.Property("NODRV")			& ";" & _
		Session.Property("DBG")
End Sub


Sub FileDelete(filename)
    Dim fso
    Set fso = CreateObject("Scripting.FileSystemObject")
    Err.clear
    If fso.FileExists(filename) Then
	    On Error Resume Next 
	    fso.DeleteFile(filename),True
	    If (Err And Err.Number <> 70) Then  ' tolerate protection errors
	  	    ErrMsg ("Could not delete: " & filename)
        End If 
    End If
End Sub


Sub FileDeleteQ(fso,filename)
    Err.clear
    If fso.FileExists(filename) Then
	    On Error Resume Next 
	    fso.DeleteFile(filename),True
	    If (Err And Err.Number <> 70) Then  ' tolerate protection errors
	  	    ErrMsg ("Could not delete: " & filename)
        End If 
    End If
End Sub


' Move and then Delete a file. File is moved into %TEMP%\basename(filename)
' then deleted; pesky files in 'system32\drivers'.

Function FileMove(filename,destination)
    Dim fso
    Set fso = CreateObject("Scripting.FileSystemObject")
    On Error Resume Next 
    If fso.FileExists(filename) Then
	    fso.MoveFile filename,destination
	    If (Err And Err.Number <> 70) then ' tolerate protection errors.
            ErrMsg ("Could not move: " & filename & " to " & destination)
        End if
    End If
    If Err Then ShowError
End Function


Sub DriverFileDelete(fso,WshShell,filename)
    Err.clear
    If fso.FileExists(filename) Then
		' allow continuation after 'permission denied' error
	    On Error Resume Next 
		' unlock the driver file by deleting PnPLocked reg entry.
		base = "reg delete HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Setup\PnpLockdownFiles /v "
		Return = WshShell.Run (base & filename & " /f", WindowStyle, true)
	    fso.DeleteFile(filename),True
	    If (Err And Err.Number <> 70) Then  ' tolerate protection errors
	  	    ErrMsg ("Could not delete: " & filename)
        End If 
    End If
End Sub



' Remove the specified folder and all sub-folders & files. 
' What rmdir does from the cmd line but will not do from vbs?

Sub RemoveFolder(objStartFolder)
    
    Set objFSO = CreateObject("Scripting.FileSystemObject")
    
    If Not objFSO.FolderExists(objStartFolder) Then
        Exit Sub
    End if
    
    Set objFolder = objFSO.GetFolder(objStartFolder)
    'Wscript.Echo objFolder.Path
    Set colFiles = objFolder.Files
    
    ' del files in top-level folder
    For Each objFile in colFiles
        objFSO.DeleteFile(objFolder.Path & "\" & objFile.Name)
        If Err Then
            ErrMsg("Del err on " & objFolder.Path & "\" & objFile.Name)
        End if
    Next
    
    ShowSubfolders objFSO.GetFolder(objStartFolder), objFSO
    
    On Error Resume Next
    objFSO.DeleteFolder(objStartFolder)
    If Err Then
        ErrMsg("DelFolder err on " & objStartFolder)
    End if
    
End Sub


Sub ShowSubFolders(Folder,FSO)
    On Error Resume Next
    For Each Subfolder in Folder.SubFolders
        ' Wscript.Echo Subfolder.Path
        Set objFolder = FSO.GetFolder(Subfolder.Path)
        Set colFiles = objFolder.Files
        For Each objFile in colFiles
            ' Wscript.Echo Subfolder.Path & "\" & objFile.Name
            FSO.DeleteFile(Subfolder.Path & "\" & objFile.Name)
            If Err Then
                ErrMsg("DelFile err on " & Subfolder.Path & "\" & objFile.Name)
            End if
        Next
        ' Wscript.Echo
        ShowSubFolders Subfolder, FSO
        FSO.DeleteFolder(Subfolder.Path)
        If Err Then
            ErrMsg("DelFolder err on " & Subfolder.Path)
        End if
    Next
End Sub




''''''''''' Remove Driver Files ''''''''''''

' Attempt to clean out driver installed files which fail to be uninstalled
' when the driver is uninstalled.
 
Sub RemoveDriverFiles(fso,WshShell)

    Dim Win, sDRIVERS, sSYS32, sSYSWOW64
    Dim CheckMode, PropArray, sTemp
	
    ' Function can be called from the Driver{Install/Uninstall} rtns.

	Win = fso.GetSpecialFolder(0) & "\"

	' this is screw-ball: on 64-bit systems: SystemFolder == %windir%\SysWOW64
	' on 32-bit systems: SystemFolder == %windir%\system32

	sSYS32 = Win & "system32\"
	sSYSWOW64 = Win & "SysWOW64\"
    sDRIVERS = sSYS32 & "drivers\"

    DriverFileDelete fso,WshShell,sDRIVERS & "ipoib.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "ibiou.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "ibsrp.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "vnic.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "qlgcvnic.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "winverbs.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "winmad.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "mthca.sys"
    FileDeleteQ fso,sDRIVERS & "mthca.sy1"
    DriverFileDelete fso,WshShell,sDRIVERS & "ndfltr.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "mlx4_bus.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "mlx4_hca.sys"
    DriverFileDelete fso,WshShell,sDRIVERS & "ibbus.sys"
    
    DriverFileDelete fso,WshShell,sSYS32 & "libibverbs.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "winmad.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "winverbs.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ibal.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ibal32.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "complib.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "cl32.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "mthcau.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "mthca32.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "mlx4u.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "mlx4nd.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "mlx4u32.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ibsrp.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ibsrpd.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "IbInstaller.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ibwsd.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ibndprov.dll"
    DriverFileDelete fso,WshShell,sSYS32 & "ndinstall.exe"
    DriverFileDelete fso,WshShell,sSYS32 & "wvndpov.dll"

    If fso.FolderExists(sSYSWOW64) Then
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "ibal.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "complib.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "mthcau.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "mlx4u.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "ibsrp.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "IbInstaller.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "ibwsd.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "ibndprov.dll"
    	DriverFileDelete fso,WshShell,sSYSWOW64 & "wvndprov.dll"
    End If

    If fso.FolderExists(Win & "lastgood" ) Then
    	FileDeleteQ fso,Win & "lastgood\system32\ibal.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\complib.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\winverbs.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\winmad.dll"

    	FileDeleteQ fso,Win & "lastgood\system32\ibndprov.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\wvndprov.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\ndinstall.exe"
    	FileDeleteQ fso,Win & "lastgood\system32\ibwsd.dll"

    	FileDeleteQ fso,Win & "lastgood\SysWOW64\ibndprov.dll"
    	FileDeleteQ fso,Win & "lastgood\SysWOW64\wvndprov.dll"
    	FileDeleteQ fso,Win & "lastgood\SysWOW64\ibwsd.dll"
    	FileDeleteQ fso,Win & "lastgood\SysWOW64\mthcau.dll"
    	FileDeleteQ fso,Win & "lastgood\SysWOW64\mlx4u.dll"

    	FileDeleteQ fso,Win & "lastgood\system32\mthcau.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\mlx4nd.dll"

    	FileDeleteQ fso,Win & "lastgood\system32\drivers\ipoib.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\ibbus.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\mthcau.dll"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\mthca.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\mlx4u.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\mlx4_bus.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\mlx4_hca.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\ndfltr.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\winverbs.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\winverbsd.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\winmad.sys"
    	FileDeleteQ fso,Win & "lastgood\system32\drivers\winmadd.sys"
    End If

    FileDeleteQ fso,Win & "winverbs.lib"
    FileDeleteQ fso,Win & "libibverbs.lib"
    
    ' delete opensm files
    sTemp = fso.GetSpecialFolder(0) & "\temp\"
    ' remove files from %SystemRoot%\temp
    FileDeleteQ fso,sTemp & "guid2lid"
    FileDeleteQ fso,sTemp & "opensm-sa.dump"
    FileDeleteQ fso,sTemp & "osm.log"
    FileDeleteQ fso,sTemp & "osm-subnet.lst"
    
End Sub


''''''''''' Delete registry key ''''''''''''

Function DeleteRegKey(KeyPath)
   Const HKEY_LOCAL_MACHINE = &H80000002
   dim strComputer
   strComputer = "."
   Set objReg=GetObject("winmgmts:" & _
       "{impersonationLevel=impersonate}!\\" & _
       strComputer & "\root\default:StdRegProv")

   ' Display error number and description if applicable
   ' If Err Then ShowError
   Return = objReg.DeleteKey(HKEY_LOCAL_MACHINE, KeyPath)
   
End Function


''''''''''' Delete registry value ''''''''''''

Function DeleteRegValue(strKeyPath, strValueName)
   Const HKEY_LOCAL_MACHINE = &H80000002
   
   dim strComputer
   strComputer = "."
   
   Set objReg=GetObject("winmgmts:" & _
       "{impersonationLevel=impersonate}!\\" & _ 
       strComputer & "\root\default:StdRegProv")

    
   Return = objReg.DeleteValue(HKEY_LOCAL_MACHINE, strKeyPath, strValueName)
   ' Display error number and description if applicable
   If Err Then ShowError
'    If (Return = 0) And (Err.Number = 0) Then
'           WScript.Echo value & "Registry value HKEY_LOCAL_MACHINE," & _
'             strKeyPath & "," & strValueName & "," & dwValue & " deleted"
'    Else
'           WScript.Echo "Registry value not deleted" & VBNewLine & _
'             "Error = " & Err.Number
'    End If

End Function



Function ReadSysPath

    Const HKEY_LOCAL_MACHINE = &H80000002
    Dim strComputer, strKeyPath, strValueName, strValue

    ReadSysPath = Null  ' assume the worst.
    strComputer = "."
    Set oReg = GetObject("winmgmts:{impersonationLevel=impersonate}!\\" & _ 
                        strComputer & "\root\default:StdRegProv")
 
    strKeyPath="SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
    strValueName = "Path"
    oReg.GetExpandedStringValue HKEY_LOCAL_MACHINE,_
         strKeyPath, strValueName, strValue

    If (Err.Number = 0) And (Not IsNull(strValue)) Then
        ReadSysPath = strValue
    End if
End Function


Function WriteSysPath(NewPath)

    Const HKEY_LOCAL_MACHINE = &H80000002
    Dim strComputer, strKeyPath, strValueName

    strComputer = "."
    Set oReg = GetObject("winmgmts:{impersonationLevel=impersonate}!\\" & _ 
                        strComputer & "\root\default:StdRegProv")
 
    strKeyPath="SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
    strValueName = "Path"
    oReg.SetExpandedStringValue _
         HKEY_LOCAL_MACHINE, strKeyPath, strValueName, NewPath

    WriteSysPath = Err.Number
End Function

' not used
''''''''''' Check installation status ''''''''''''

Function install_verify()
   Dim Status
   Dim sInstalldir
   sInstalldir = Session.Property("INSTALLDIR")
   Set WshShell = CreateObject("WScript.Shell")
   Set vstat = WshShell.Exec(sInstalldir & "vstat.exe")
   install_verify = vstat.ExitCode
End Function

'-------------------------------------------------------------

' add registry key
Function CreateRegKey(KeyPath)
   Const HKEY_LOCAL_MACHINE = &H80000002
   dim strComputer
   strComputer = "."
   Set objReg=GetObject("winmgmts:" & _
       "{impersonationLevel=impersonate}!\\" & _
       strComputer & "\root\default:StdRegProv")

   ' Display error number and description if applicable
   If Err Then ShowError
   Return = objReg.CreateKey(HKEY_LOCAL_MACHINE, KeyPath)
   
End Function



'--------------------------------------------------------


' Function to add registry DWORD val.
Function AddRegDWORDValue(strKeyPath, strValueName, dwValue)
    Const HKEY_LOCAL_MACHINE = &H80000002
    strComputer = "."
 
    Set oReg=GetObject("winmgmts:{impersonationLevel=impersonate}!\\" &_ 
                       strComputer & "\root\default:StdRegProv")
    If Err Then ShowError
 
    oReg.SetDWORDValue HKEY_LOCAL_MACHINE,strKeyPath,strValueName,dwValue

    If Err Then ShowError

End Function

'-------------------------------------------------

' Function to add registry Expanded string val.

Function AddRegExpandValue(strKeyPath, strValueName, dwValue)
    Const HKEY_LOCAL_MACHINE = &H80000002
    strComputer = "."
 
    Set oReg=GetObject("winmgmts:{impersonationLevel=impersonate}!\\" &_ 
            strComputer & "\root\default:StdRegProv")
 
    If Err Then ShowError
    oReg.SetExpandedStringValue HKEY_LOCAL_MACHINE,strKeyPath,strValueName,_
         dwValue
    If Err Then ShowError

End Function

'------------------------------------------------------------------------


' Function to add registry string val.

Function AddRegStringValue(strKeyPath, strValueName, dwValue)
Const HKEY_LOCAL_MACHINE = &H80000002
strComputer = "."
 
Set oReg=GetObject("winmgmts:{impersonationLevel=impersonate}!\\" &_ 
strComputer & "\root\default:StdRegProv")
 
If Err Then ShowError
oReg.SetStringValue HKEY_LOCAL_MACHINE,strKeyPath,strValueName,dwValue
If Err Then ShowError

End Function

'------------------------------------------------------------------------

' Return a list of PCI devices using 'devcon find | findall'
' sFindHow - stringArg: devcon cmd arg {find | findall}

Function Find_Dev_by_Tag(WshShell,exe,sFindHow,tag)
    Dim cmd

	cmd = exe & " " & sFindHow & " * | find """ & tag & """"
	Set connExec = WshShell.Exec(cmd)
    If Err Then
        msgbox "Shell.Exec err: " & cmd
		Find_Dev_by_Tag = Null
        Exit Function
    End if

	devs = split(connExec.StdOut.ReadAll, vbCrLF)

	On Error Resume Next 

	' verify we have some useful data.
	dim arrSize
	arrSize = 0
	for each dev in devs
		If Instr(dev,Tag) <> 0 Then
			arrSize = arrSize + 1
		End if
	next

	If arrSize = 0 Then
		Find_Dev_by_Tag = Null
		Exit Function
	End If
		
	'Create new array of selected devices
	dim ibaDev()
	Redim ibaDev(arrSize - 1)
	index = 0
	For each dev in devs
		if (Instr(dev,Tag) <> 0) Then
			ibaDev(index) = dev
			index = index + 1
		End if
	Next

	Find_Dev_by_Tag = ibaDev

End Function



Function dpinst_Install_VNIC(WshShell,sInstalldir)
	Dim dpinstVNIC,cmd,rc

	dpinst_Install_VNIC = 0

   	dpinstVNIC = "cmd.exe /c cd /d " & sInstalldir & _
					"qlgcvnic & ..\dpinst.exe "

	cmd = dpinstVNIC & "/S /F /SA /PATH """ & sInstalldir & _
					"Drivers\qlgcvnic"" /SE /SW"
	rc = WshShell.Run (cmd,WindowStyle,true)
	If (rc AND DPINST_INSTALLED) = 0 Then
		dpinst_status "qlgcvnic Install failed",cmd,rc,"dpinst_Install_VNIC"
		dpinst_Install_VNIC = rc
	ElseIf sDBG >= "1" Then
		dpinst_status "qlgcvnic Install OK.",cmd,rc,"dpinst_Install_VNIC"
	End If

End Function



Function dpinst_Install_SRP(WshShell,sInstalldir)
	Dim dpinstSRP,cmd,rc

	dpinst_Install_SRP = 0
   	dpinstSRP = "cmd.exe /c cd /d " & sInstalldir _
				& "SRP & ..\dpinst.exe "
	cmd = dpinstSRP & "/S /F /SA /PATH """ & sInstalldir & "Drivers\SRP""" & " /SE /SW"
	rc = WshShell.Run (cmd,WindowStyle,true)
	If (rc AND DPINST_INSTALLED) = 0 Then
		dpinst_status "SRP Install failed",cmd,rc,"dpinst_Install_SRP"
		dpinst_Install_SRP = rc
	ElseIf sDBG >= "1" Then
		dpinst_status "SRP Install OK.",cmd,rc,"dpinst_Install_SRP"
	End if

End Function



' For installer error codes see
'  http://msdn2.microsoft.com/en-us/library/aa368542(VS.85).aspx 

Const ERROR_INSTALL_SOURCE_ABSENT = 1612  ' missing files to install,
                                              ' invalid feature selection.
Const ERROR_INSTALL_FAILURE       = 1603  ' fatal error during installation
Const ERROR_FUNCTION_FAILED       = 1627  ' function failed during execution   
Const ERROR_SUCCESS_REBOOT_REQUIRED = 3010 ' restart required

' For the dpinst.exe error discussion see
' http://msdn.microsoft.com/en-us/library/ms791066.aspx 
'
' The dpinst.exe return code is a DWORD (0xWWXXYYZZ), where the meaning of
' the four single-byte fields 0xWW, 0xXX, 0xYY, and 0xZZ are defined as follows

' 0xWW If a driver package could not be installed, the 0x80 bit is set. If a
'      computer restart is necessary, the 0x40 bit is set. Otherwise, no bits
'      are set. 
' 0xXX The number of driver packages that could not be installed. 
' 0xYY The number of driver packages that were copied to the driver store but
'      were not installed on a device. 
' 0xZZ The number of driver packages that were installed on a device. 

Const DPINST_INSTALLED = &H000000FF

Sub dpinst_status(umsg,cmd,err,title)

	Dim I,S(4)

	msg = umsg & " (status 0x" & Hex(err) & ") " & vbCrLf & vbCrLf & _
			cmd & vbCrLf & _
			"  Details in %windir%\dpinst.log" & vbCrLf & _
			"          or %windir%\inf\setupapi.dev.log" & vbCrLf & vbCrLf
	For I = 0 To 3
		S(I) = (err AND 255)
		err = err / 256
	Next
	msg = msg & "Status Decode:" & vbCrLf
	msg = msg & S(0) & " driver packages installed on a device" & vbcrlf & _
		S(1) & " driver packages copied to the driver store and not installed on a device" & vbcrlf & _
		S(2) &   " driver packages not installed on a device" & vbcrlf
	if S(3) = &H80 then
            msg = msg & "[0x" & Hex(S(3)) & "] A driver package could not be installed." & vbcrlf
	end if
	if S(3) = &H40 then
            msg = msg & "[0x" & Hex(S(3)) & "] 0x40 reboot required." & vbcrlf
	end if

	msgbox msg,,title
 
End Sub



''''''''''' Post Device Driver Install ''''''''''''

Function PostDriverInstall()
    Dim CheckMode, PropArray
	Dim VersionNT, InstallThis, localSM, Rsockets
    Dim rc, cmd, sInstalldir, fso

	On Error Resume Next

    ' Get the value of INSTALLDIR - see WinOF_setup
    CheckMode = Session.Property("CustomActionData")

    If CheckMode <> "" Then
        'in defered action this is the way to pass arguments.
	    PropArray = Split(Session.Property("CustomActionData"), ";")
    Else
        Redim PropArray(9)
	PropArray(0) = Session.Property("INSTALLDIR") 
	PropArray(1) = Session.Property("SystemFolder") 
    	PropArray(2) = Session.Property("System64Folder") 
	PropArray(3) = Session.Property("WindowsFolder")
	PropArray(4) = Session.Property("VersionNT")
	PropArray(5) = Session.Property("ADDLOCAL")
	PropArray(6) = Session.Property("REMOVE")
	PropArray(7) = Session.Property("NODRV")
	PropArray(8) = Session.Property("DBG")
    End If

    ' If cmd-line specified NODRV=1, then do not install drivers.
    ' Should not get here with NODRV=1 as WIX src files check.
    ' Be safe.

    If PropArray(7) <> "" Then
    	Exit Function
    End If

    sInstalldir = PropArray(0)
    VersionNT	= PropArray(4)
    InstallThis	= PropArray(5)
    sDBG	= PropArray(8) ' set global debug flag.
    localSM	= instr(InstallThis,"fOSMS")
    Rsockets    = instr(InstallThis,"fRsock")

    Set WshShell = CreateObject("WScript.Shell")
    Set fso = CreateObject("Scripting.FileSystemObject")
	
    err.clear 
	        
    ' DIFxApp (Driver Install Frameworks for Applications)
    ' http://www.microsoft.com/whdc/driver/install/DIFxFAQ.mspx#E4AAC 
    ' DIFxApp.wixlib has already installed driver to Driver Store; PNP will
    ' handle the actual device driver install/rollback on failure.

    ' OpenSM Subnet Manager service was already created in the disabled state.
    ' Should the Local OpenSM Subnet Manager service be started/enabled?

    If localSM Then
    	OpenSM_StartUp WshShell,sInstalldir
    	If sDBG >= "1" Then
    	    msgbox "Local Subnet Management Service [OpenSM] started.",,_
    	    		"PostDriverInstall"
    	    MsiLogInfo "[PostDriverInstall] Local Subnet Management Service [OpenSM] started."
	    End If
    End If

    ' RSockets service was already created in the disabled state in earlier phase.
    ' Should the RSockets service be started/enabled?

    If Rsockets Then
    	RsocketsService_StartUp WshShell,sInstalldir
        If sDBG >= "1" Then
    	    msgbox "Rsockets Helper Service [Rsock] started.",,_
    	    		"PostDriverInstall"
            MsiLogInfo "[PostDriverInstall] Rsockets Helper Service [Rsock] started."
     	End If
    End If

    PostDriverInstall = 0

End Function



'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

' called from WIX src to Disable/Remove any IB Services such that driver removal
' can proceed successfully (no dependencies).
' shutdown Rsockets, NetworkDirect & Winsock Direct providers in
' order to remove their references to the rest of the IB stack such that the
' IB stack/drivers can be removed.

Sub Remove_Services()  
	Dim WshShell, fso, sInstalldir, winDir, sVersionNT, rc

	Set WshShell = CreateObject("WScript.Shell")
	Set fso = CreateObject("Scripting.FileSystemObject")

	' Check if install was done with NODRV=1, if so then nothing to do, exit
	If Session.Property("NODRV") = "1" Then
		Exit Sub
	End If

	sInstalldir = Session.Property("INSTALLDIR")
	sVersionNT = Session.Property("VersionNT")
	winDir = Session.Property("WindowsFolder")

	If fso.FileExists(winDir & "system32\ndinstall.exe") Then
	    cmd = "ndinstall -q -r mlx4nd2 & ndinstall -q -r ibal"
	    rc = WshShell.Run ("cmd.exe /c " & cmd, WindowStyle, true)
	End If
	
    ' remove the Rsockets Helper Service
	If fso.FileExists(sInstalldir & "rsocksvc.exe") Then
		rc = WshShell.Run ("cmd.exe /c cd /d " & sInstalldir & _
	                       " & rsocksvc.exe -remove", WindowStyle, true)
	End If

    ' remove the Rsockets service provider
	If fso.FileExists(sInstalldir & "rsinstall.exe") Then
		rc = WshShell.Run ("cmd.exe /c cd /d " & sInstalldir & _
	                       " & rsinstall.exe -r", WindowStyle, true)
	End If

	' remove the WinSock Direct service.

	If fso.FileExists(sInstalldir & "installsp.exe") Then
		rc = WshShell.Run ("cmd.exe /c cd /d " & sInstalldir & _
		                       " & installsp.exe -r", WindowStyle, true)
	End If

End Sub


' called from WIX src to Disable/Remove IB devices so actual driver removal
' can proceed correctly.
' Actual driver file cleanup is handled after DIFxApp processing.
' shutdown NetworkDirect & Winsock Direct providers in
' order to remove their references to the rest of the IB stack such that the
' IB stack/drivers can be removed.

Sub Remove_IB_Devices()  
	Dim WshShell, fso, sInstalldir, winDir, sVersionNT, rc

	Set WshShell = CreateObject("WScript.Shell")
	Set fso = CreateObject("Scripting.FileSystemObject")

	' Check if install was done with NODRV=1, if so then nothing to do, exit
	If Session.Property("NODRV") = "1" Then
		Exit Sub
	End If

	sInstalldir = Session.Property("INSTALLDIR")

	Uninstall_IB_Devices fso,WshShell,sInstalldir

End Sub


' Find all IBA devices [IBA,PCI\VEN_15B3,MLX4] using devman.exe
' In devman.exe output, Device tags all start in col #1.

Function Find_IBA_Devices(WshShell,sInstalldir) 
	Dim dev

	Set ibaDevicesExec = WshShell.Exec ("cmd.exe /c cd /D " & sInstalldir & "Drivers & devman.exe findall * ")

	ibaDevices = split(ibaDevicesExec.StdOut.ReadAll, vbCrLF)

	' Determine the actual array Size - dump nonessential lines from cmd output.
	dim arrSize
	arrSize = 0
	For each dev in ibaDevices
		If (Instr(dev,"IBA\") = 1) Then
			arrSize = arrSize + 1
		ElseIf (Instr(dev,"PCI\VEN_15B3") = 1) Then
			arrSize = arrSize + 1
		ElseIf (Instr(dev,"MLX4") = 1) Then
			arrSize = arrSize + 1
		End if
	Next

	If arrSize = 0 Then
		Find_IBA_Devices = Null
		Exit Function
	End If
		
	'Creating array of IBA\ devices
	dim ibaDev()
	Redim ibaDev(arrSize - 1)
	index = 0
	For each dev in ibaDevices
		If (Instr(dev,"IBA\") = 1) Then
			ibaDev(index) = dev
			index = index + 1
		ElseIf (Instr(dev,"PCI\VEN_15B3") = 1) Then
			ibaDev(index) = dev
			index = index + 1
		ElseIf (Instr(dev,"MLX4") = 1) Then
			ibaDev(index) = dev
			index = index + 1
		End if
	Next

	Find_IBA_Devices=ibaDev 

End Function


' returns an array of all Local Area Connections which
' were created for IPoIB.

Function Find_IPOIB_LAC()
	Dim WinOS,cmd,base,dev

	Set WshShell = CreateObject("WScript.Shell")
	base = "cmd.exe /c reg query HKLM\SYSTEM\CurrentControlSet\Control\Network"
	WinOS = Session.Property("VersionNT")

	If (WinOS <> WindowsXP) Then
		' Win2K3 style
		cmd = base & " /f ""IBA\IPOIB"" /s /d | FIND ""Connection"" "
	Else
		' XP style
		cmd = base & " /s | FIND ""}\Connection"" " 
	End if

	Set ibaDevicesExec = WshShell.Exec ("cmd.exe /C " & cmd)

	ibaDevices = split(ibaDevicesExec.StdOut.ReadAll, vbCrLF)

	' determine the array Size
	dim arrSize
	arrSize = 0
	for each dev in ibaDevices
		arrSize = arrSize + 1
	next
	'Creating array of Local Area Connections based on IPoIB
	dim ibaDev()
	Redim ibaDev(arrSize - 1)
	index = 0

	For each dev in ibaDevices
		If dev = "" Then
		ElseIf WinOS <> WindowsXP then
			' ibaDev(index) = dev
			delstr = Left(dev,Len(dev)-Len("\Connection"))
			ibaDev(index) = delstr
			index = index + 1
		Else
			' XP reg.exe format sucks, unable to filter IBA\IPOIB, so we do
			' it here....sigh.
			Set rex = WshShell.Exec ("cmd.exe /C reg.exe query " & dev & _
									" /v PnpInstanceID | FIND ""IBA\IPOIB"" ")
		    resp = split(rex.StdOut.ReadAll, vbCrLF)
		    For each re in resp
'				msgbox "XP dev " & dev
'				msgbox "XP re " & re
			    if Instr(re,"IPOIB") Then
			        delstr = Left(dev,Len(dev)-Len("\Connection"))
				    ibaDev(index) = delstr
				    index = index + 1
   	             Exit For
			    End If
			next
		End if
	next
	Find_IPOIB_LAC=ibaDev

End Function


' Remove 3rd party (OEM) driver package; files are identified by containing
' the string LookFor.

Sub find_remove_INF_file(WshShell,exe,LookFor)

    Dim cmd,cmdDM,use_dpinst,pfile,found

	' using dpinst.exe[WLH] or devman.exe[wnet/xp]?
	use_dpinst = Instr(exe,"dpinst")

	cmd = "cmd.exe /c for /f %i in ('findstr /m /c:""" & LookFor _
			& """ %WINDIR%\inf\oem*.inf') do @echo %i"

    Set infFilesExec = WshShell.Exec ( cmd )

    InfFiles = infFilesExec.StdOut.ReadAll
    IFILES = Split(InfFiles,vbCrLf)
	found = 0
	On Error Resume Next 
    For Each file in IFILES
        If (file <> "") Then
			' most common is devman.exe 
           	cmd = exe & " -f dp_delete " & file
			if use_dpinst then
				cmdDM = replace(cmd,"dpinst","devman")
				cmd = exe & " /U """ & file & """ /S /D"
			end if
			If sDBG >= "1" Then
				msgbox "Found '" & LookFor & _
						"' in file" & vbCrLf & "  " & file & vbCrLf & _
						"  " & cmd,,"find_remove_INF_file"
				found = 1
			End If
           	Return = WshShell.Run (cmd, WindowStyle, true)
			if use_dpinst then
				' use devman.exe to delete all .inf referenced files
				Return = WshShell.Run (cmdDM, WindowStyle, true)
			end if
			' make sure the .inf & .pnf files are removed.
			pfile = replace(file,".inf",".pnf")
			FileDelete file
			FileDelete pfile
        End IF
    Next

	If sDBG > "1" AND found = 0 Then
		msgbox "Did not find " & LookFor,,"find_remove_INF_file" 
	End If

End Sub


Sub remove_INF_file(WshShell,exe,file)

    Dim cmd,cmdDM,use_dpinst,pfile

	' using dpinst.exe[WLH] or devman.exe[wnet/xp]?
	use_dpinst = Instr(exe,"dpinst")

	On Error Resume Next 

	' most common is devman.exe 
   	cmd = exe & " -f dp_delete " & file
	If use_dpinst Then
		cmdDM = replace(cmd,"dpinst","devman")
		cmd = exe & " /U """ & file & """ /S /D"
	end if

	If sDBG >= "1" Then
		msgbox "Removing driver package " & file & vbCrLf & "  " & cmd,,_
				"remove_INF_file"
	end if

	Return = WshShell.Run (cmd, WindowStyle, true)
	if use_dpinst then
		' use devman.exe to delete all .inf referenced files
		Return = WshShell.Run (cmdDM, WindowStyle, true)
	end if

	' make sure the .inf & .pnf files are removed.
	pfile = replace(file,".inf",".pnf")
	FileDelete file
	FileDelete pfile

End Sub


Sub cleanup_driver_files(fso,WshShell,sInstalldir,tool,devInfo)

	Dim i,Flist,udfCnt

	If IsNull(devInfo) Then
		msgbox "cleanup_driver_files <null> devInfo?"
		Exit Sub
	End If

	If sDBG >= "1" Then
		DisplayDevInfo devInfo,"OFED Remove Driver Package"
		udfCnt = 0
		Flist = ""
	End If

	' Device Install Info
	'(0) Device-ID
	'(1) Device name
	'(2) Fully qualified INF filename
	'(3) count of files installed from .inf file.
	'(4) thru (3) [fileCount] Fully qualified Installed driver filenames.

	remove_INF_file WshShell,tool,devInfo(2)

	For i=4 To DevInfo(3) + 3
		' skip the KMDF wdmcoinstaller*.dll file as we do not ref count here
		' and could break other installed KMDF drivers if removed.
		If Instr(devinfo(i),"WdfCoInstaller") = 0 Then
    		DriverFileDelete fso,WshShell,devInfo(i)
			If sDBG >= "1" Then
				If fso.FileExists(devInfo(i)) Then
					Flist = Flist & "  " & devInfo(i) & vbCrLf
					udfCnt = udfCnt + 1
				End If
			End If
		End If
	Next

	If sDBG >= "1" Then
		msgbox devInfo(1) & vbCrLf & devInfo(2) & vbCrLf & _
			"Remaining Driver Package Files " & udfCnt & vbCrLf & Flist,,_
			"cleanup_driver_files"
	End If

End Sub


' Not used - run the specified command during next startup.

Function RunAtReboot(name,run_once_cmd)
	dim key_name
	key_name = "Software\Microsoft\Windows\CurrentVersion\RunOnce"
	AddRegStringValue key_name,name,run_once_cmd
	msgbox "RunAtReboot( " & name & " , " & run_once_cmd & " )"
    RunAtReboot = 0
End Function

' DEBUG
Sub DisplayDevInfo(DevInfo,suffix)

	Dim i,devs

	If IsNull(DevInfo) Then
		Exit Sub
	End If
	devs = ""
	For i=4 To DevInfo(3) + 3
		devs = devs & "    " & DevInfo(i) & vbCRLF 
	Next

	msgbox  "[DeviceID]     " & DevInfo(0) & vbCRLF &_
			"[DeviceName]   " & DevInfo(1) & vbCRLF &_
			"[INF filename] " & DevInfo(2) & vbCRLF &_
			"[INF installed files] " & DevInfo(3) & vbCRLF &_
			devs,,suffix
End Sub


Function GetDeviceInstallInfo(WshShell,sInstalldir,Dev)

	Dim devman,tmp,s,StrStart,StrEnd,FileCnt,INF

	devman = "cmd.exe /c cd /d " & sInstalldir & _
				"Drivers & devman.exe driverfiles ""@"

	Set connExec = WshShell.Exec(devman & Dev & """")
	cmdout = split(connExec.StdOut.ReadAll, vbCrLF)
	If IsNull(cmdout) Then
		GetDeviceInstallInfo = Null
		Exit Function
	End If

	StrStart = InStr(1,cmdout(2),"installed from ") + 15
	If StrStart <= 15 Then
		GetDeviceInstallInfo = Null
		Exit Function
	End If

	StrEnd = InStr(StrStart,cmdout(2),"[") - 1
	INF = mid(cmdout(2),StrStart,StrEnd-StrStart)

	StrStart = InStr(StrEnd,cmdout(2),"]. ") + 3
	StrEnd = InStr(StrStart,cmdout(2)," file")
	FileCnt = CInt(mid(cmdout(2),StrStart,StrEnd-StrStart))

	'(0) Device-ID
	'(1) Device name
	'(2) Fully qualified INF filename
	'(3) count of files installed from .inf file.
	'(4) thru (3) [fileCount] Fully qualified Installed driver filenames.

	dim ibaDev()
	Redim ibaDev(3+FileCnt)
	ibaDev(0) = cmdout(0)
	tmp = ltrim(cmdout(1))
	s = Len(tmp) - (Instr(tmp,"Name: ") + 5)
	ibaDev(1) = Right(tmp,s)
	ibaDev(2) = INF
	ibaDev(3) = FileCnt

	' (ibaDev(3) - 1) + 4 == ibaDev(3) + 3
	For i=4 To FileCnt + 3
		ibaDev(i) = ltrim(cmdout(i-1))
	Next

'	DisplayDevInfo ibaDev,"OFED Device Info"

	GetDeviceInstallInfo = ibaDev

End Function



' remove IB I/O Unit driver

Sub	Uninstall_IOU(fso,WshShell,devList,sInstalldir)

	RemoveDevice fso,WshShell,sInstalldir,devList,"InfiniBand I/O Unit"

End Sub



' Remove QLogic VNIC instances

Sub Uninstall_VNIC(fso,WshShell,devices,sInstalldir)

	Dim devman,Return,device,dt,sDRIVERS,tool,devInfo

	devman = "cmd.exe /c cd /d " & sInstalldir & "Drivers & devman.exe "

	If IsNull(devices) Then
		' create a list of IBA\* devices via "devcon find"
		devices = Find_IBA_Devices(WshShell,sInstalldir)
		If IsNull(devices) Then
			Exit Sub
		End If
	End If

	devInfo = Null
	For each devTarget in devices
	    If (Instr(devTarget,"IBA\V00066AP00000030")) Then
	        device = split(devTarget, ":")
	        dt = rtrim(device(0))
			If IsNull(devInfo) Then
				devInfo = GetDeviceInstallInfo(WshShell,sInstalldir,dt)
			End If
			' disable instance - double quote complex device name for success.
			Return = WshShell.Run (devman & "disable ""@" & dt & """", _
									WindowStyle, true)
	        ' Removing the Qlogic Vnic I/O Unit
	        Return = WshShell.Run (devman & "remove ""@" & dt & """", _
									WindowStyle, true)
	    End if
	Next

End Sub


' QLogic Virtual FC I/O controller or
' InfiniBand SRP Miniport: IBA\C0100C609EP0108 or IBA\CFF00C609EP0108
' OFED SRP target: IBA\V000002P00005A44
' one driver handles all three.

SRP_IDS = Array(_
			"IBA\V000002P00005A44",_
			"IBA\C0100C609EP0108",_
			"IBA\CFF00C609EP0108",_
			"IBA\V00066AP00000038",_
			"IBA\V000006P00006282")


Sub Uninstall_SRP(fso,WshShell,devices,sInstalldir)

	Dim devman,devmanRMAT,devmanDAAT,Return,device,sDRIVERS,tool,devInfo

	' QLogic Virtual FC I/O controller or
	' InfiniBand SRP Miniport: IBA\C0100C609EP0108 or IBA\CFF00C609EP0108
	' one driver handles all three.
	' See previous SRP_IDS definition @ Install_SRP.

	devman = "cmd.exe /c cd /d " & sInstalldir & "Drivers & devman.exe "
	devmanRMAT = devman & "remove @"
	devmanDAAT = devman & "disable @"
	devInfo = Null

	If IsNull(devices) Then
		' create a list of IBA\* devices via "devcon find"
		devices = Find_IBA_Devices(WshShell,sInstalldir)
		If IsNull(devices) Then
			Exit Sub
		End If
	End If

	' Remove SRP devices
	'	QLogic Virtual FC I/O controller instance?
	'	Either: IBA\C0100C609EP0108 or IBA\CFF00C609EP0108
	'	Linux SRP target: IBA\V000002P00005A44
	For each ID in SRP_IDS
		For each deviceCan in devices
			If Instr(deviceCan,ID) <> 0 Then
				device = split(deviceCan, ":")
		        dt = rtrim(device(0))
				If IsNull(devInfo) Then
					devInfo = GetDeviceInstallInfo(WshShell,sInstalldir,dt)
				End If
				' disable the instance
				Return = WshShell.Run (devmanDAAT & dt, WindowStyle, true)
				' Removing SRP device
				Return = WshShell.Run (devmanRMAT & dt, WindowStyle, true)
				' msgbox "Uninstall_SRP() " & devmanRMAT & dt & " rc " & Return
			End if
		Next
	Next

End Sub


Sub RemoveDevice(fso,WshShell,sInstalldir,devList,DeviceTag)

	dim device,devman,devmanRMAT,devTarget,dt,Return,devInfo

	devman = "cmd.exe /c cd /d " & sInstalldir & "Drivers & devman.exe "
	devmanRMAT = devman & "remove ""@"
	devmanDAAT = devman & "disable ""@"
	devInfo = Null

	If IsNull(devList) Then
		devList = Find_Dev_by_Tag(WshShell,devman,"findall",DeviceTag)
		If IsNull(devList) Then
			If sDBG > "1" Then
				msgbox "No device by tag '" & DeviceTag & "' ?",,"RemoveDevice"
			End If
			Exit Sub
		End If
	End If

	For each devTarget in devList
	    If Instr(devTarget,DeviceTag) Then
	        device = split(devTarget, ":")
	        dt = rtrim(device(0))
			If IsNull(devInfo) Then
				devInfo = GetDeviceInstallInfo(WshShell,sInstalldir,dt)
			End If
	        Return = WshShell.Run (devmanDAAT & dt & """", WindowStyle, true)
	        Return = WshShell.Run (devmanRMAT & dt & """", WindowStyle, true)
	    End if
	Next

End Sub


Sub Uninstall_IB_Devices(fso,WshShell,sInstalldir)

	Dim devList

	If (fso.FileExists(sInstalldir & "Drivers\dpinst.exe") = False) Then
        msgbox "Uninstall_IB_Devices() Error " & sInstalldir & _
				"Drivers\dpinst.exe Not Found?"
	    Exit Sub ' no reason to continue without the tool.
	End if

	If (fso.FileExists(sInstalldir & "Drivers\devman.exe") = False) Then
	    Exit Sub ' no reason to continue without the tool.
	End if

	' create a list of IBA\* devices via "devcon find"

	devList = Find_IBA_Devices(WshShell,sInstalldir)
	If Not IsNull(devList) Then
		Uninstall_SRP fso,WshShell,devList,sInstalldir

		Uninstall_VNIC fso,WshShell,devList,sInstalldir

		' remove I/O Unit driver
		Uninstall_IOU fso,WshShell,devList,sInstalldir

		' remove IPoIB devices
		RemoveDevice fso,WshShell,sInstalldir,devList,"IBA\IPOIB"
    End If

	' stop the openSM service in case it was started.
	Return = WshShell.Run ("cmd.exe /c sc.exe stop opensm", WindowStyle, true)

	' delete opensm service from registry
	Return = WshShell.Run ("cmd.exe /c sc.exe delete opensm", WindowStyle, true)

	' remove HCA devices

	If Not IsNull(devList) Then
	    RemoveDevice fso,WshShell,sInstalldir,devList,"MLX4\CONNECTX_HCA"
	    ' VEN_15B3 covers devices: mthca & mlx4_bus
	    RemoveDevice fso,WshShell,sInstalldir,devList,"PCI\VEN_15B3"
    End If

End Sub



''''''''''' Driver Cleanup ''''''''''''
' called from WIX src to cleanup after IB driver uninstall.
' Assumption is NetworkDirect and Winsock Direct have been shutdown to remove
' their IB stack references.

Sub IB_DriverCleanup()  
	Dim sInstalldir, WshShell, fso, sRemove,devman, tool

	sInstalldir = Session.Property("INSTALLDIR")

	Set WshShell = CreateObject("WScript.Shell")
	Set fso = CreateObject("Scripting.FileSystemObject")

	' Check if install was done with NODRV=1, if so then nothing to do, exit
	If Session.Property("NODRV") = "1" Then
		Exit Sub
	End If

	sDBG = Session.Property("DBG")

	sRemove = Session.Property("REMOVE")
	If sRemove = "" Then
		sRemove = "ALL"
	End If

	' Remove Service entries from the registry

	DeleteRegKey "System\CurrentControlSet\Services\ibbus"
	DeleteRegKey "System\CurrentControlSet\Services\mthca"
	DeleteRegKey "System\CurrentControlSet\Services\mlx4_bus"
	DeleteRegKey "System\CurrentControlSet\Services\mlx4_hca"
	DeleteRegKey "System\CurrentControlSet\Services\ipoib"
	DeleteRegKey "System\CurrentControlSet\Services\ibiou"
	DeleteRegKey "System\CurrentControlSet\Services\ibsrp"
	DeleteRegKey "System\CurrentControlSet\Services\qlgcvnic"
	DeleteRegKey "System\CurrentControlSet\Services\winverbs"
	DeleteRegKey "System\CurrentControlSet\Services\winmad"

	' Mthca
	DeleteRegValue "SYSTEM\CurrentControlSet\Control\CoDeviceInstallers" ,_
		 			"{58517E00-D3CF-40c9-A679-CEE5752F4491}"

	DeleteRegKey "SYSTEM\CurrentControlSet\Control\Class\{58517E00-D3CF-40C9-A679-CEE5752F4491}" 

	' Connectx (mlx4)
	DeleteRegValue "SYSTEM\CurrentControlSet\Control\CoDeviceInstallers" ,_
					 "{31B0B28A-26FF-4dca-A6FA-E767C7DFBA20}"      

	DeleteRegKey "SYSTEM\CurrentControlSet\Control\Class\{31B0B28A-26FF-4dca-A6FA-E767C7DFBA20}"

	DeleteRegKey "SYSTEM\CurrentControlSet\Control\Class\{714995B2-CD65-4a47-BCFE-95AC73A0D780}"
	
	' In livefish mode, the above does not always work - just in case.
	' remove reg entries for ConnectX, mthca, ibbus, mlx4 & ipoib

	nukem = Array(_
			"Control\Class\{58517E00-D3CF-40C9-A679-CEE5752F4491}",_
			"Control\Class\{31B0B28A-26FF-4dca-A6FA-E767C7DFBA20}",_
			"Control\Class\{714995B2-CD65-4a47-BCFE-95AC73A0D780}",_
			"Services\ibbus",_
			"Services\mthca",_
			"Services\mlx4_bus",_
			"Services\mlx4_hca",_
			"Services\ipoib",_
			"Services\EventLog\System\ipoib",_
			"Services\ibiou",_
			"Services\qlgcvnic",_
			"Services\ibsrp",_
			"Services\winmad",_
			"Services\winverbs",_
			"Services\eventlog\System\mthca",_
			"Services\eventlog\System\mlx4_bus",_
			"Services\eventlog\System\mlx4_hca",_
			"Services\eventlog\System\ibbus" )

	base = "reg.exe delete HKLM\SYSTEM\CurrentControlSet\" 

	' in livefish mode the delete didn't suceed, delete it in another way
    For each ID in nukem
		If ID <> "" Then
			Return = WshShell.Run (base & ID & " /f", WindowStyle, true)
		End if
	Next

	' Cleanup KMDF CoInstaller Driver entries.
	Return = WshShell.Run (base & "Control\Wdf\Kmdf\kmdflibrary\versions\1\driverservices /v mlx4_bus /f", WindowStyle, true)
	Return = WshShell.Run (base & "Control\Wdf\Kmdf\kmdflibrary\versions\1\driverservices /v winverbs /f", WindowStyle, true)
	Return = WshShell.Run (base & "Control\Wdf\Kmdf\kmdflibrary\versions\1\driverservices /v winmad /f", WindowStyle, true)

' Not yet
'	Return = WshShell.Run ("reg.exe delete HKLM\Software\Microsoft\Windows\currentVersion\DIFx\DriverStore\WinVerbs_* /f", WindowStyle, true)
'	Return = WshShell.Run ("reg.exe delete HKLM\Software\Microsoft\Windows\currentVersion\DIFx\DriverStore\ipoib_* /f", WindowStyle, true)
'

	' Remove all Local Area Connection Registry entries which were constructed
	' for IPoIB. Next OFED install gets same IPoIB local area connection
	' assignment.

	Dim IPOIB_LAC
	IPOIB_LAC = Find_IPOIB_LAC

	For each LAC in IPOIB_LAC
		If LAC <> "" Then
			Return = WshShell.Run ("reg.exe delete " & LAC & " /f", _
									WindowStyle, true)
			If Err Then ShowErr
		End if
	Next

	Session.Property("REBOOT") = "FORCE"
	err.clear 

	' cleanup INF files

	If (fso.FileExists(sInstalldir & "Drivers\dpinst.exe") = False) Then
	    Exit Sub ' no reason to continue without the tool.
	End if

	devman = "cmd.exe /c cd /d " & sInstalldir & "Drivers & devman.exe "

	' use dpinst.exe instead of devman.exe for Windows LongHorn++
	tool = "cmd.exe /c cd /d " & sInstalldir & "Drivers & dpinst.exe "

	find_remove_INF_file WshShell,tool,"mthca"
	find_remove_INF_file WshShell,tool,"mlx4_hca"
	find_remove_INF_file WshShell,tool,"mlx4_bus"

	' catch-all cleanup.
	find_remove_INF_file WshShell,devman,"Mellanox"
	find_remove_INF_file WshShell,devman,"InfiniBand"

	' remove driver installed files
	RemoveDriverFiles fso,WshShell
	
	err.clear 

End Sub


' HCA load failure?
' Check for [SystemFolder]\complib.dll. If not preset then ConnectX HCA
' driver failed to install; see mlx4_hca.inf. Complib.dll absence occurs
' when ConnectX bus driver detects invalid Firmware hence does not create
' the PDO for the ConnectX HCA device so PNP never loads the HCA driver and
' complib.dll never gets installed.
' If features[IPoIB+(WinSockDirect OR NetworkDirect)] are installed, then
' wait for IPoIB device to appear in order to ensure WSD and/or ND provider
' install success.

Sub CheckDriversOK()
    Dim winDir, WshShell, fso, AddLocal, too_long

	Set WshShell = CreateObject("WScript.Shell")
	Set fso = CreateObject("Scripting.FileSystemObject")

	winDir = Session.Property("WindowsFolder")
	sInstalldir = Session.Property("INSTALLDIR")
	devman = "cmd.exe /c cd /d " & sInstalldir & "Drivers & devman.exe "
	AddLocal = Session.Property("ADDLOCAL")

	' Check if HCA driver installed OK.
	If Not fso.FileExists(winDir & "system32\complib.dll") Then
		' SW only install, such that there are no hardware HCAs avaiable?
		devList = Find_Dev_by_Tag(WshShell,devman,"find","PCI\VEN_15B3")
		If IsNull(devList) Then
			Exit Sub
		End If

		' 10 sec timeout warning box.
		WshShell.Popup "WARNING: Possible HCA Driver Startup Failure." _
			& vbCrLf & "  Consult the Windows System Event log! (mlx4_bus)",_
			10,"OFED-Install CheckDriversOK",vbInformation+vbSystemModal

		MsiLogInfo "[CheckDriversOK] ** Possible HCA Driver Startup Failure" & _
					vbCrLf & "[CheckDriversOK] **   Consult System Event Log."
	    Exit Sub
	End If

	' empty string implies default install Features which include IPoIB+WSD+ND.
	If AddLocal <> "" Then
		' No wait if !IPoIB OR (!WinSockDirect AND !NetworkDirect)
		If Instr(AddLocal,"fIPoIB") = 0 Then
			Exit Sub
		End If
		If Instr(AddLocal,"fWSD") = 0 AND Instr(AddLocal,"fND") = 0 Then
			Exit Sub
		End If
	End If

	' wait for IPoIB driver to start so ND and/or WSD can install
    ' Define start as the appearance of IBA\IPOIB instance in device database.

	devList = Find_Dev_by_Tag(WshShell,devman,"find","IBA\IPOIB")

    too_long = 15
	If Session.Property("VersionNT") = WindowsXP Then
        ' XP does not support timeout cmd; let timeout cmd fail to consume time.
        too_long = 50
    End If

	Do While IsNull(devList)
      ' Wait for device IPoIB to appear as it's required for WSD/ND install.
      WshShell.Run "cmd.exe /c timeout /T 2", WindowStyle, true
	  too_long = too_long - 1
	  if too_long <= 0 then
		' timeout info box.
		WshShell.Popup "WARNING: Possible NetworkDirect startup Failure." _
			& vbCrLf & "  Waited too long for IBA\IPOIB device?" & vbCrLf & _
            "use 'ndinstall -l' to verify NetworkDirect provider " & _
            "installed correctly.",15,"OFED-Install CheckDriversOK", _
            vbInformation+vbSystemModal

		MsiLogInfo "[OFED] ** Possible NetworkDirect startup Failure" & _
			vbCrLf & "[OFED] **   Waited too long for IBA\IPOIB device?" & _
			vbCrLf & "[OFED] **   Check ND provider status."
	  	exit Do
	  End If
	  err.clear 
	  devList = Find_Dev_by_Tag(WshShell,devman,"find","IBA\IPOIB")
	Loop 

End Sub


' Enable WSD if installsp.exe was installed (feature Winsock direct selected).
' For Windows XP, this CustomAction should not be called as WSD is not
' supported on XP.

Sub WSDEnable()
	Dim sInstalldir, WshShell, fso

	sInstalldir = Session.Property("INSTALLDIR")

	Set WshShell = CreateObject("WScript.Shell")
	Set fso = CreateObject("Scripting.FileSystemObject")

	' Check if install was done with NODRV=1, if so then nothing to do, exit
	If Session.Property("NODRV") = "1" Then
		Exit Sub
	End If

	If fso.FileExists(sInstalldir & "installsp.exe") Then
		' install the WinSockdirect service
		Return = WshShell.Run ("cmd.exe /c cd /d " & sInstalldir _
                                 & " & installsp.exe -i", WindowStyle, true)
	End If

End Sub


' This sub will only be called if the feature ND start was selected.
' See WIX src file - ND_start

Sub ND_StartMeUp()
    Dim Ret, sInstalldir, NDprovider, winDir, WshShell, fso

    sInstalldir = Session.Property("INSTALLDIR")

    Set WshShell = CreateObject("WScript.Shell")
    Set fso = CreateObject("Scripting.FileSystemObject")
	winDir = Session.Property("WindowsFolder")

    ' Start the Network Direct Service if installed
    If fso.FileExists(winDir & "system32\ndinstall.exe") Then
	    ' ia64 only supports the ND/winverbs provider, otherwise use
        ' ND/ibal provider.
	    NDprovider = "mlx4nd2"
	    If Architecture() = "ia64" Then
		    NDprovider = "winverbs"
	    End If
        Ret = WshShell.Run ("cmd.exe /c ndinstall -q -i " & NDprovider, _
							WindowStyle, true)
        If Ret Then ShowErr2("ND service provider install failed")
    End If

End Sub


' Convert the disabled OpenSM Windows service to an 'auto' startup on next boot.
' OpenSM service was created by WIX installer directives - see WOF.wxs file.
' Performs local service conversion and then starts the local OpenSM service.
' Routine called from PostDriverInstall phase 'IF' a local OpenSM service was
' requested. The point is to get a local OpenSM up and running prior to loading
' the IOU and SRP/VNIC drivers. Server 2003 IOU driver load fails if OpenSM
' has not yet registered the IOC?

Sub OpenSM_StartUp(WshShell,sInstalldir)

    Return = WshShell.Run ("cmd.exe /c sc.exe config opensm start= auto", _
							WindowStyle,true)
    Return = WshShell.Run ("cmd.exe /c sc.exe start opensm", WindowStyle, true)
    Err.clear

End Sub


' Routine called from PostDriverInstall phase 'IF' a local RSockets service was
' requested.
' Install librdmacm as a service provider
' Start Rsockets Helper service

Sub RsocketsService_StartUp(WshShell,sInstalldir)
    Dim cmd

   	cmd = "cmd.exe /c cd /d " & sInstalldir &  " & rsinstall.exe -i"
    Return = WshShell.Run (cmd, WindowStyle, true)

   	cmd = "cmd.exe /c cd /d " & sInstalldir &  " & rsocksvc.exe -install"
    Return = WshShell.Run (cmd, WindowStyle, true)

    Err.clear

End Sub


Sub ScheduleLocalReboot
		
    Set objWMILocator = CreateObject ("WbemScripting.SWbemLocator") 
      objWMILocator.Security_.Privileges.AddAsString "SeShutdownPrivilege",True 
    Set objWMIServices = objWMILocator.ConnectServer(strComputerName, _
    			cWMINameSpace, strUserID, strPassword)
    Set objSystemSet = GetObject(_
    	"winmgmts:{impersonationLevel=impersonate,(Shutdown)}")._
				InstancesOf("Win32_OperatingSystem")

    ' Forced restart
    For Each objSystem In objSystemSet
	objSystem.Win32Shutdown 2+4
	objSystem.Win32Shutdown 2+4
	objSystem.Win32Shutdown 2+4
    Next
		
    'msgbox "Please wait while computer restarts ...",0,"OFED"

End Sub


' Now that WIX+[Windows Installer] handle previous installs, this routine
' only deletes lingering driver files which were not removed from the last
' uninstall.
' Called in immediate mode, condition: INSTALL=1

Function ChkPreviousInstall()

    Set fso = CreateObject("Scripting.FileSystemObject")    
    Set WshShell = CreateObject("WScript.Shell")

	' Check if install was done with NODRV=1, if so then nothing to do, exit
	If Session.Property("NODRV") = "1" Then
		Exit Function
	End If

	' remove any lingering driver installed files
	RemoveDriverFiles fso,WshShell
	
	ChkPreviousInstall = 0

End Function



' Not Used - idea was to run %SystemRoot%\temp\OFEDcleanup.bat on the next
' reboot to remove driver files which did not get uninstalled (win2K3 only);
' script ran, files persisted on Win2K3?

Sub RunOnceCleanup(fso,sInstalldir)

  Dim sTemp, cmd, script

  On Error Resume Next

  If Session.Property("REMOVE") <> "ALL" Then
	' XXX
	msgbox "RunOnceCleanup - not remove ALL?"
	Exit Sub
  End if

  script = "RunOnceOFEDcleanup.bat"
  src = sInstalldir & "Drivers\" & script

  If Not fso.FileExists(src) Then
	msgbox "Missing " & src
	Exit Sub
  End if

  ' copy OFEDclean.bat to %SystemRoot%\temp for runOnce cmd
  sTemp = fso.GetSpecialFolder(0) & "\temp\" & script
  If fso.FileExists(sTemp) Then
	Err.clear
	fso.DeleteFile(sTemp),True
  End If

  Err.clear
  fso.CopyFile src, sTemp 
  If Err.Number = 0 Then
      cmd = "cmd.exe /C " & sTemp
      RunAtReboot "OFED", cmd
      ' 2nd cmd to remove previous script.
' XXX      cmd = "cmd.exe /C del /F/Q " & sTemp
'      RunAtReboot "OFED2", cmd
  End if

End Sub



' WIX has appended [INSTALLDIR] to the system search path via <Environment>.
' Unfortunately WIX does not _Broadcast_ to all top-level windows the registry
' Settings for '%PATH%' have changed. Run nsc to do the broadcast.
 
Sub BcastRegChanged
    Dim sInstalldir
    Set WshShell=CreateObject("WScript.Shell")

    sInstalldir = Session.Property("INSTALLDIR")

	On Error Resume Next 
    WshShell.Run "%COMSPEC% /c cd /d " & sInstalldir & " & nsc.exe",  _
					WindowStyle, true
    Err.clear
    BcastRegChanged = 0

End Sub


' This routine should never be...InstallShield-12 for some reason has
' decided not to completely remove [INSTALLDIR]? Until such a time
' that 'why' is understood, this routine removes [INSTALLDIR]! Sigh...
' nuke some driver files which remain due to devcon device install.
' Immediate execution; called after MsiCleanupOnSuccess (REMOVE="ALL")

Sub HammerTime
    Dim fso, sInstalldir, rc, cmd, win

    Set fso=CreateObject("Scripting.FileSystemObject") 
    Set WshShell=CreateObject("WScript.Shell")

    sInstalldir = Session.Property("INSTALLDIR")
    If fso.FolderExists(sInstalldir) Then
        cmd = "cmd.exe /c rmdir /S /Q """ & sInstalldir & """"
        rc = wshShell.Run(cmd,WindowStyle,true)
    End if

    If fso.FolderExists("C:\OFED_SDK") Then
    	RemoveFolder "C:\OFED_SDK"
    End if

    ' System32 driver cruft - ref counting errors
	Win = fso.GetSpecialFolder(0) & "\" & "system32\"

    FileDeleteQ fso,Win & "mlx4u.dll"
    FileDeleteQ fso,Win & "mlx4nd.dll"
    FileDeleteQ fso,Win & "ndinstall.exe"

    Win = Win & "drivers\"
    FileDeleteQ fso,Win & "ipoib.sys"
    FileDeleteQ fso,Win & "winmad.sys"
    FileDeleteQ fso,Win & "winverbs.sys"
    FileDeleteQ fso,Win & "ndfltr.sys"
    FileDeleteQ fso,Win & "mlx4_bus.sys"
    FileDeleteQ fso,Win & "ibbus.sys"

End Sub



' NOT USED - deferred action to increment ticks while action is taking place
'
Function AddProgressInfo( )
	Const INSTALLMESSAGE_ACTIONSTART = &H08000000
	Const INSTALLMESSAGE_ACTIONDATA  = &H09000000 
	Const INSTALLMESSAGE_PROGRESS    = &H0A000000 

	Set rec = Installer.CreateRecord(3)
	
	rec.StringData(1) = "callAddProgressInfo"
	rec.StringData(2) = "Incrementing the progress bar..."
	rec.StringData(3) = "Incrementing tick [1] of [2]"
	
	'Message INSTALLMESSAGE_ACTIONSTART, rec
	
	rec.IntegerData(1) = 1
	rec.IntegerData(2) = 1
	rec.IntegerData(3) = 0
	
	Message INSTALLMESSAGE_PROGRESS, rec
	
	Set progrec = Installer.CreateRecord(3)
	
	progrec.IntegerData(1) = 2
	progrec.IntegerData(2) = 5000
	progrec.IntegerData(3) = 0
	
	rec.IntegerData(2) = 1500000
	
	For i = 0 To 5000000 Step 5000
	    rec.IntegerData(1) = i
	    ' action data appears only if a text control subscribes to it
	    ' Message INSTALLMESSAGE_ACTIONDATA, rec
	    Message INSTALLMESSAGE_PROGRESS, progrec
	Next ' i
	
	' return success to MSI
	AddProgressInfo = 0
End Function


' Called when .msi 'CHANGE' (ADD/REMOVE) installation was selected.
' Currently only handles SRP & VNIC

Function InstallChanged

    Dim rc, sInstalldir, sRemove, sDRIVERS, NeedReboot
    Err.clear

    sRemove = Session.Property("REMOVE")
	If sRemove = "ALL" Then
		Exit Function
	End If

    sDBG = Session.Property("DBG")

    Set WshShell=CreateObject("WScript.Shell")
    Set fso = CreateObject("Scripting.FileSystemObject")

	NeedReboot = 0

    sInstalldir = Session.Property("INSTALLDIR")

	On Error Resume Next 

	' Nothing to do for ADD as DIFxAPP has loaded drivers into Driver Store.

	' For REMOVE - cleanup

	If (Not IsNull(sRemove)) AND (sRemove <> "") Then

		If Instr(sRemove,"fSRP") Then
			Uninstall_SRP fso,WshShell,Null,sInstalldir
			NeedReboot = NeedReboot + 1
		End If

		If Instr(sRemove,"fVNIC") Then
			Uninstall_VNIC fso,WshShell,Null,sInstalldir
			NeedReboot = NeedReboot + 1
		End If
	End If

	If NeedReboot Then
		Session.Property("REBOOT") = "FORCE"	  
		InstallChanged = ERROR_SUCCESS_REBOOT_REQUIRED
		' until forced reboot relly works....
		msgbox "A system reboot is required to complete this operation." _
				& vbCrLf & "Please do so at your earliest convinence."
	End If

End Function

