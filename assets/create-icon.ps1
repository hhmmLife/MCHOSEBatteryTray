$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$taskImages = @()
foreach ($taskSize in @(16,24,32,48,64,256)) {
    $taskBitmap = [System.Drawing.Bitmap]::new($taskSize,$taskSize)
    $taskGraphics = [System.Drawing.Graphics]::FromImage($taskBitmap)
    $taskGraphics.SmoothingMode = 'AntiAlias'
    $taskGraphics.ScaleTransform($taskSize/32.0,$taskSize/32.0)
    $taskPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $taskPath.AddArc(7,1,18,18,180,180)
    $taskPath.AddLine(25,10,25,22)
    $taskPath.AddArc(7,13,18,18,0,180)
    $taskPath.CloseFigure()
    $taskBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(25,230,181))
    $taskPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(40,50,58),1.5)
    $taskGraphics.FillPath($taskBrush,$taskPath)
    $taskGraphics.DrawPath($taskPen,$taskPath)
    $taskGraphics.DrawLine($taskPen,7,11,25,11)
    $taskGraphics.DrawLine($taskPen,16,2,16,10)
    $taskBrush.Color = [System.Drawing.Color]::FromArgb(40,50,58)
    $taskGraphics.FillRectangle($taskBrush,14.8,4,2.4,4)
    $taskStream = [System.IO.MemoryStream]::new()
    $taskBitmap.Save($taskStream,[System.Drawing.Imaging.ImageFormat]::Png)
    $taskImages += ,@($taskSize,$taskStream.ToArray())
    $taskStream.Dispose(); $taskPen.Dispose(); $taskBrush.Dispose(); $taskPath.Dispose(); $taskGraphics.Dispose(); $taskBitmap.Dispose()
}
$taskOutput = [System.IO.File]::Create((Join-Path $PSScriptRoot 'app.ico'))
$taskWriter = [System.IO.BinaryWriter]::new($taskOutput)
try {
    $taskWriter.Write([uint16]0); $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]$taskImages.Count)
    $taskOffset = 6 + 16*$taskImages.Count
    foreach ($taskImage in $taskImages) {
        $taskDimension = if($taskImage[0] -eq 256){0}else{$taskImage[0]}
        $taskWriter.Write([byte]$taskDimension); $taskWriter.Write([byte]$taskDimension)
        $taskWriter.Write([byte]0); $taskWriter.Write([byte]0); $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]32)
        $taskWriter.Write([uint32]$taskImage[1].Length); $taskWriter.Write([uint32]$taskOffset)
        $taskOffset += $taskImage[1].Length
    }
    foreach ($taskImage in $taskImages) { $taskWriter.Write([byte[]]$taskImage[1]) }
} finally { $taskWriter.Dispose(); $taskOutput.Dispose() }
