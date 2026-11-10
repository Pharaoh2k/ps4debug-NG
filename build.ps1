# SPDX-License-Identifier: GPL-3.0-only
#
# build.ps1 - native Windows DEV-CHECK build of the ps4debug-NG payload.
#
# This is NOT a release build. Release builds use the pinned WSL toolchain
# (build.sh: Ubuntu 24.04 + GCC 13.3.0). This script exists so the payload can
# be compile- and link-checked on a stock Windows box with only LLVM installed
# (clang + lld emit FreeBSD ELF directly; no WSL, no gcc, no make required).
#
# Usage:  powershell -File build.ps1 [-Clean]
#
# Override the LLVM location with $env:PS4DEBUG_LLVM if clang is not on PATH
# and not under "C:\Program Files\LLVM\bin".

#Requires -Version 5.1
[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# ------------------------------------------------------------------ toolchain
$llvm = $env:PS4DEBUG_LLVM
if (-not $llvm) {
    $onPath = Get-Command clang.exe -ErrorAction SilentlyContinue
    if ($onPath) { $llvm = Split-Path -Parent $onPath.Source }
}
if (-not $llvm) { $llvm = 'C:\Program Files\LLVM\bin' }

$clang   = Join-Path $llvm 'clang.exe'
$llvmAr  = Join-Path $llvm 'llvm-ar.exe'
$llvmObjcopy = Join-Path $llvm 'llvm-objcopy.exe'
$llvmStrip   = Join-Path $llvm 'llvm-strip.exe'
$llvmReadobj = Join-Path $llvm 'llvm-readobj.exe'

foreach ($tool in @($clang, $llvmAr, $llvmObjcopy, $llvmStrip)) {
    if (-not (Test-Path $tool)) {
        Write-Error "missing LLVM tool: $tool`nInstall LLVM or set PS4DEBUG_LLVM."
    }
}
Write-Host "toolchain: $clang" -ForegroundColor DarkGray

# ------------------------------------------------------------------- flags
# Mirrors the per-component Makefiles. GCC-only flags are dropped where clang
# has no equivalent (noted inline); they do not change the dev-check result.
$target = '--target=x86_64-pc-freebsd12'
# ksdk/kdebugger/installer inline asm is AT&T (gcc auto-translates %0 operand
# templates across dialects; clang does not), and their .s files carry their
# own .intel_syntax directive - so they are built without global -masm=intel.
$archIntel    = @('-march=btver2', '-mtune=btver2', '-m64', '-masm=intel', '-mcmodel=small')
$archNoIntel  = @('-march=btver2', '-mtune=btver2', '-m64', '-mcmodel=small')
$tailFlags    = @('-ffunction-sections', '-fdata-sections', '-Wall', '-D__dev_t=dev_t')
$common       = @($target, '-nostdlib', '-nostartfiles', '-fno-builtin') + $archIntel + $tailFlags
$commonNoIntel = @($target, '-nostdlib', '-nostartfiles', '-fno-builtin') + $archNoIntel + $tailFlags
$gnu11 = '-std=gnu11'
$c11   = '-std=c11'

function Invoke-CCompile([string]$src, [string]$obj, [string[]]$flags) {
    & $clang -c $src -o $obj @flags | Write-Verbose
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $src" }
}

function New-StaticLib([string]$dir, [string]$libName, [string[]]$flags) {
    $srcDir = Join-Path $dir 'source'
    $outDir = Join-Path $dir 'build'
    if ($Clean -and (Test-Path $outDir)) { Remove-Item -Recurse -Force $outDir }
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $objs = @()
    Get-ChildItem $srcDir -Filter *.c | ForEach-Object {
        $obj = Join-Path $outDir ($_.BaseName + '.o')
        Invoke-CCompile $_.FullName $obj ($flags + $gnu11)
        $objs += $obj
    }
    Get-ChildItem $srcDir -Filter *.s | ForEach-Object {
        $obj = Join-Path $outDir ($_.BaseName + '.o')
        Invoke-CCompile $_.FullName $obj ($flags + $gnu11)
        $objs += $obj
    }
    $lib = Join-Path $dir $libName
    if (Test-Path $lib) { Remove-Item $lib }
    & $llvmAr rcs $lib @objs | Write-Verbose
    if ($LASTEXITCODE -ne 0) { throw "ar failed: $lib" }
    Write-Host "  -> $libName" -ForegroundColor Green
}

function Invoke-LldLink([string]$tmp, [string]$crt0, [string[]]$objs, [string[]]$linkFlags) {
    & $clang $target -nostdlib -fuse-ld=lld $crt0 @objs -o $tmp @linkFlags | Write-Verbose
    if ($LASTEXITCODE -ne 0) { throw "link failed: $tmp" }
}

Write-Host "ps4debug-ng (Windows dev-check) building..." -ForegroundColor Cyan

$ksdkDir   = Join-Path $root 'ps4-ksdk'
$libPs4Dir = Join-Path $root 'ps4-payload-sdk/libPS4'

# ------------------------------------------------------------------- ksdk
Write-Host "=> ps4-ksdk" -ForegroundColor Yellow
# gcc's -fno-zero-initialized-in-bss has no clang equivalent; dropped.
New-StaticLib $ksdkDir 'libKSDK.a' `
    (@('-I', (Join-Path $ksdkDir 'include'), '-I', $ksdkDir, '-Os', '-fPIE') + $commonNoIntel)

# ------------------------------------------------------------------ libPS4
Write-Host "=> libPS4" -ForegroundColor Yellow
New-StaticLib $libPs4Dir 'libPS4.a' `
    (@('-I', (Join-Path $libPs4Dir 'include'), '-Os', '-fPIE') + $common)

# ----------------------------------------------------------------- kdebugger
Write-Host "=> kdebugger" -ForegroundColor Yellow
$kdir = Join-Path $root 'kdebugger'
$kFlags = @('-I', (Join-Path $ksdkDir 'include'), '-I', $kdir,
            '-I', (Join-Path $kdir 'include'),
            '-O3', '-fPIE', '-fno-exceptions', '-fno-asynchronous-unwind-tables') + $commonNoIntel
$kOut = Join-Path $kdir 'build'
if ($Clean -and (Test-Path $kOut)) { Remove-Item -Recurse -Force $kOut }
New-Item -ItemType Directory -Force $kOut | Out-Null
$kObjs = @()
Get-ChildItem (Join-Path $kdir 'source') -Filter *.c | ForEach-Object {
    $obj = Join-Path $kOut ($_.BaseName + '.o')
    Invoke-CCompile $_.FullName $obj ($kFlags + $gnu11)
    $kObjs += $obj
}
$kElf = Join-Path $kdir 'kdebugger.elf'
$kTmp = Join-Path $kdir 'kdebugger.tmp.elf'
Invoke-LldLink $kTmp (Join-Path $kdir 'crt0.s') $kObjs `
    @('-L', $ksdkDir, '-L', $kdir, '-lKSDK',
      '-Wl,-T,' + (Join-Path $kdir 'kdebugger.x'),
      '-Wl,--build-id=none', '-pie')
# The Linux Makefile runs `elfedit --output-type=DYN` after linking; lld -pie
# already emits ET_DYN, so the step is a no-op here (type printed for parity).
$et = (& $llvmReadobj --file-headers $kTmp | Select-String 'Type:').ToString().Trim()
Write-Host "   $et" -ForegroundColor DarkGray
Move-Item -Force $kTmp $kElf
& $llvmStrip $kElf | Write-Verbose
Write-Host "  -> kdebugger.elf" -ForegroundColor Green

# ----------------------------------------------------------------- debugger
Write-Host "=> debugger" -ForegroundColor Yellow
$ddir = Join-Path $root 'debugger'
$zydisDir = Join-Path $ddir 'third_party/zydis'
$ksDir    = Join-Path $ddir 'third_party/keystone'
$cxxrtDir = Join-Path $ddir 'third_party/cxxrt'
$dFlags = @('-I', (Join-Path $libPs4Dir 'include'), '-I', $ddir,
            '-I', (Join-Path $ddir 'include'), '-I', $zydisDir,
            '-I', (Join-Path $ksDir 'include'),
            '-O2', '-fPIE', '-DZYAN_NO_LIBC', '-D__MM_MALLOC_H') + $common
$dOut = Join-Path $ddir 'build'
if ($Clean -and (Test-Path $dOut)) { Remove-Item -Recurse -Force $dOut }
New-Item -ItemType Directory -Force $dOut | Out-Null
$dObjs = @()
Get-ChildItem (Join-Path $ddir 'source') -Filter *.c | ForEach-Object {
    $obj = Join-Path $dOut ($_.BaseName + '.o')
    Invoke-CCompile $_.FullName $obj ($dFlags + $c11)
    $dObjs += $obj
}
Invoke-CCompile (Join-Path $zydisDir 'Zydis.c') (Join-Path $dOut 'Zydis.o') `
    (@('-I', $zydisDir, '-O3', '-DNDEBUG', '-DZYAN_NO_LIBC', '-w', '-fPIE') + $common)
$dObjs += (Join-Path $dOut 'Zydis.o')
# The prebuilt cxxrt archives carry a DT_NEEDED on "pthread" (FreeBSD merges
# libpthread into libc; there is no libpthread to find). An empty archive
# satisfies the specifier under lld.
$stubDir = Join-Path $root 'build-support/windows-lib'
New-Item -ItemType Directory -Force $stubDir | Out-Null
$stubPthread = Join-Path $stubDir 'libpthread.a'
if (-not (Test-Path $stubPthread)) { & $llvmAr rcs $stubPthread | Write-Verbose }
$dTmp = Join-Path $ddir 'debugger.tmp.elf'
Invoke-LldLink $dTmp (Join-Path $libPs4Dir 'crt0.s') $dObjs `
    @('-L', $libPs4Dir, '-L', (Join-Path $ddir 'lib'), '-L', $stubDir,
      '-L', (Join-Path $cxxrtDir 'lib'), '-L', (Join-Path $ksDir 'lib'),
      '-lPS4', '-Wl,--start-group', '-lkeystone', '-lc++', '-lc++abi', '-lunwind',
      '-Wl,--end-group',
      '-pie', '-Wl,-T,' + (Join-Path $libPs4Dir 'linker.x'),
      '-Wl,--build-id=none', '-Wl,--gc-sections', '-Wl,--no-dynamic-linker',
      '-Wl,-z,noseparate-code', '-Wl,-z,norelro', '-Wl,-z,max-page-size=0x1000')
$dbgBin = Join-Path $ddir 'debugger.bin'
& $llvmObjcopy -O binary $dTmp $dbgBin
if ($LASTEXITCODE -ne 0) { throw 'objcopy failed: debugger.bin' }
Remove-Item $dTmp
Write-Host "  -> debugger.bin" -ForegroundColor Green

# ----------------------------------------------------------------- installer
Write-Host "=> installer" -ForegroundColor Yellow
$idir = Join-Path $root 'installer'
# installer.c embeds the userland debugger via `extern uint8_t debuggerbin[]`
# and `extern int32_t debuggerbin_size`. No Makefile rule generates these; the
# embedding object is created here from debugger/debugger.bin.
$iOut = Join-Path $idir 'build'
if ($Clean -and (Test-Path $iOut)) { Remove-Item -Recurse -Force $iOut }
New-Item -ItemType Directory -Force $iOut | Out-Null
$embedAsm = @"
.globl debuggerbin
.globl debuggerbin_size
.section .rodata
debuggerbin:
.incbin "$(($dbgBin -replace '\\', '/'))"
debuggerbin_end:
.data
.align 4
debuggerbin_size:
.int debuggerbin_end - debuggerbin
"@
$embedSrc = Join-Path $iOut 'debuggerbin.s'
Set-Content -Path $embedSrc -Value $embedAsm -NoNewline
$embedObj = Join-Path $iOut 'debuggerbin.o'
Invoke-CCompile $embedSrc $embedObj @($target, '-c')

# installer.c also embeds kdebugger.elf as kernelelf[]/kernelelf_size.
$kElfEmbed = @"
.globl kernelelf
.globl kernelelf_size
.section .rodata
kernelelf:
.incbin "$(($kElf -replace '\\', '/'))"
kernelelf_end:
.data
.align 4
kernelelf_size:
.int kernelelf_end - kernelelf
"@
$kEmbedSrc = Join-Path $iOut 'kernelelf.s'
Set-Content -Path $kEmbedSrc -Value $kElfEmbed -NoNewline
$kEmbedObj = Join-Path $iOut 'kernelelf.o'
Invoke-CCompile $kEmbedSrc $kEmbedObj @($target, '-c')

$iFlags = @('-I', (Join-Path $ksdkDir 'include'), '-I', $idir,
            '-I', (Join-Path $idir 'include'), '-Os', '-fpie') + $commonNoIntel
$iObjs = @()
Get-ChildItem (Join-Path $idir 'source') -Filter *.c | ForEach-Object {
    $obj = Join-Path $iOut ($_.BaseName + '.o')
    Invoke-CCompile $_.FullName $obj ($iFlags + $gnu11)
    $iObjs += $obj
}
$iObjs += $embedObj
$iObjs += $kEmbedObj
# installer/include/syscall.h generates stubs that jmp syscall_macro and
# main.c calls syscall(), but libKSDK does not define either symbol (a gap in
# the Linux build as well - flagging upstream). libPS4's syscall.s provides
# the canonical implementation under the same header convention.
$syscallObj = Join-Path $iOut 'libps4_syscall.o'
Invoke-CCompile (Join-Path $libPs4Dir 'source/syscall.s') $syscallObj @($target, $gnu11)
$stubObj = Join-Path $iOut 'devcheck_stubs.o'
Invoke-CCompile (Join-Path $root 'build-support/devcheck_stubs.s') $stubObj @($target, '-c')
$iTmp = Join-Path $idir 'installer.tmp.elf'
Invoke-LldLink $iTmp (Join-Path $idir 'crt0.s') $iObjs `
    @('-L', $ksdkDir, '-L', $idir, $syscallObj, $stubObj, '-lKSDK',
      '-Wl,-T,' + (Join-Path $idir 'linker.x'),
      '-Wl,--build-id=none', '-Wl,--gc-sections')
$iBin = Join-Path $idir 'installer.bin'
& $llvmObjcopy -O binary $iTmp $iBin
if ($LASTEXITCODE -ne 0) { throw 'objcopy failed: installer.bin' }
Remove-Item $iTmp
Write-Host "  -> installer.bin" -ForegroundColor Green

Copy-Item $iBin (Join-Path $root 'ps4debug-ng.bin') -Force
Write-Host ""
Write-Host "dev-check build complete (NOT a release build)." -ForegroundColor Cyan
Write-Host "outputs: kdebugger/kdebugger.elf, debugger/debugger.bin," -ForegroundColor DarkGray
Write-Host "        installer/installer.bin, ps4debug-ng.bin" -ForegroundColor DarkGray
