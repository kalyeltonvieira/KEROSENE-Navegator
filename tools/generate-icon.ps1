$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$outPath = Join-Path $root "src\resources\kerosene.ico"
New-Item -ItemType Directory -Force -Path (Split-Path $outPath) | Out-Null

function Add-RoundRect {
    param(
        [System.Drawing.Drawing2D.GraphicsPath]$Path,
        [single]$X,
        [single]$Y,
        [single]$W,
        [single]$H,
        [single]$R
    )

    $d = $R * 2.0
    $Path.AddArc($X, $Y, $d, $d, 180, 90)
    $Path.AddArc($X + $W - $d, $Y, $d, $d, 270, 90)
    $Path.AddArc($X + $W - $d, $Y + $H - $d, $d, $d, 0, 90)
    $Path.AddArc($X, $Y + $H - $d, $d, $d, 90, 90)
    $Path.CloseFigure()
}

function New-KeroseneBitmap {
    param([int]$Size)

    $bmp = New-Object System.Drawing.Bitmap $Size, $Size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)

    $margin = [single]([Math]::Max(1.0, $Size * 0.08))
    $rect = New-Object System.Drawing.RectangleF $margin, $margin, ([single]($Size - $margin * 2.0)), ([single]($Size - $margin * 2.0))
    $radius = [single]($Size * 0.22)

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    Add-RoundRect $path $rect.X $rect.Y $rect.Width $rect.Height $radius

    $shadowRect = New-Object System.Drawing.RectangleF ([single]($rect.X + $Size * 0.025)), ([single]($rect.Y + $Size * 0.035)), $rect.Width, $rect.Height
    $shadow = New-Object System.Drawing.Drawing2D.GraphicsPath
    Add-RoundRect $shadow $shadowRect.X $shadowRect.Y $shadowRect.Width $shadowRect.Height $radius
    $shadowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(58, 0, 0, 0))
    $g.FillPath($shadowBrush, $shadow)

    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush -ArgumentList `
        $rect, `
        ([System.Drawing.Color]::FromArgb(255, 11, 13, 18)), `
        ([System.Drawing.Color]::FromArgb(255, 18, 112, 206)), `
        ([single]45.0)
    $g.FillPath($brush, $path)

    $hotRect = New-Object System.Drawing.RectangleF ([single]($rect.X + $rect.Width * 0.08)), ([single]($rect.Y + $rect.Height * 0.08)), ([single]($rect.Width * 0.84)), ([single]($rect.Height * 0.28))
    $hot = New-Object System.Drawing.Drawing2D.GraphicsPath
    Add-RoundRect $hot $hotRect.X $hotRect.Y $hotRect.Width $hotRect.Height ([single]($radius * 0.55))
    $hotBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(34, 255, 255, 255))
    $g.FillPath($hotBrush, $hot)

    $border = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(96, 255, 255, 255)), ([single]([Math]::Max(1.0, $Size / 42.0)))
    $g.DrawPath($border, $path)

    $fontSize = [single]($Size * 0.57)
    $font = New-Object System.Drawing.Font "Segoe UI", $fontSize, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $textRect = New-Object System.Drawing.RectangleF 0, ([single]($Size * 0.07)), ([single]$Size), ([single]($Size * 0.72))
    $textBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(252, 255, 255, 255))
    $g.DrawString("K", $font, $textBrush, $textRect, $format)

    if ($Size -ge 32) {
        $sig = New-Object System.Drawing.Drawing2D.GraphicsPath
        $sig.StartFigure()
        $sig.AddBezier(
            [single]($Size * 0.24), [single]($Size * 0.72),
            [single]($Size * 0.38), [single]($Size * 0.60),
            [single]($Size * 0.45), [single]($Size * 0.86),
            [single]($Size * 0.58), [single]($Size * 0.73))
        $sig.AddBezier(
            [single]($Size * 0.58), [single]($Size * 0.73),
            [single]($Size * 0.70), [single]($Size * 0.62),
            [single]($Size * 0.76), [single]($Size * 0.77),
            [single]($Size * 0.88), [single]($Size * 0.68))

        $sigPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 186, 71)), ([single]([Math]::Max(1.8, $Size * 0.055)))
        $sigPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $sigPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $sigPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $g.DrawPath($sigPen, $sig)
        $sigPen.Dispose()
        $sig.Dispose()
    }

    $format.Dispose()
    $font.Dispose()
    $textBrush.Dispose()
    $border.Dispose()
    $hotBrush.Dispose()
    $hot.Dispose()
    $brush.Dispose()
    $shadowBrush.Dispose()
    $shadow.Dispose()
    $path.Dispose()
    $g.Dispose()

    return $bmp
}

function Get-IconImageBytes {
    param([System.Drawing.Bitmap]$Bitmap)

    $size = $Bitmap.Width
    $xorSize = $size * $size * 4
    $maskStride = [int]([Math]::Floor(($size + 31) / 32) * 4)
    $maskSize = $maskStride * $size

    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $bw.Write([uint32]40)
    $bw.Write([int32]$size)
    $bw.Write([int32]($size * 2))
    $bw.Write([uint16]1)
    $bw.Write([uint16]32)
    $bw.Write([uint32]0)
    $bw.Write([uint32]$xorSize)
    $bw.Write([int32]0)
    $bw.Write([int32]0)
    $bw.Write([uint32]0)
    $bw.Write([uint32]0)

    for ($y = $size - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $size; $x++) {
            $c = $Bitmap.GetPixel($x, $y)
            $bw.Write([byte]$c.B)
            $bw.Write([byte]$c.G)
            $bw.Write([byte]$c.R)
            $bw.Write([byte]$c.A)
        }
    }

    $bw.Write((New-Object byte[] $maskSize))
    $bw.Flush()
    $bytes = $ms.ToArray()
    $bw.Dispose()
    $ms.Dispose()
    return $bytes
}

$sizes = @(16, 32, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $bmp = New-KeroseneBitmap $size
    $images += [pscustomobject]@{
        Size = $size
        Bytes = Get-IconImageBytes $bmp
    }
    $bmp.Dispose()
}

$iconStream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $iconStream
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)

$offset = 6 + (16 * $images.Count)
foreach ($image in $images) {
    $writer.Write([byte]($(if ($image.Size -ge 256) { 0 } else { $image.Size })))
    $writer.Write([byte]($(if ($image.Size -ge 256) { 0 } else { $image.Size })))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$image.Bytes.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Bytes.Length
}

foreach ($image in $images) {
    $writer.Write([byte[]]$image.Bytes)
}

$writer.Flush()
[System.IO.File]::WriteAllBytes($outPath, $iconStream.ToArray())
$writer.Dispose()
$iconStream.Dispose()

Write-Host "Generated $outPath"
