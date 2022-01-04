
' Open the file for input.

Const OpenFileForReading = 1, ForReading = 1

Dim fso, File, Filename, MyFile, TextLine, S

Set fso = CreateObject("Scripting.FileSystemObject")

Filename = ".\\about-OFED.txt"

' Read from the file and display the results.

Set File = fso.GetFile(FileName)

Set TextStream = File.OpenAsTextStream(OpenFileForReading)

Do While Not TextStream.AtEndOfStream
   TextLine = TextStream.ReadLine
   if Left(TextLine,1) <> "#" then
       S = S & TextLine & vbCr
   end if
Loop

TextStream.Close

MsgBox S, 0, "About OFED for Windows"



