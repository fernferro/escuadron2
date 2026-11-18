@echo off
rem Arma "Instalar-Escuadron2-completo.exe" con tus texturas HD y tu musica.
rem Poner en esta misma carpeta:
rem   Instalar-Escuadron2.exe
rem   texturas_hd\        (la carpeta del paquete de texturas, descomprimida)
rem   musica.flac         (o musica.ogg / musica.mp3 / musica.wav; opcional)
rem y hacer doble clic en este archivo.
cd /d "%~dp0"
if not exist "Instalar-Escuadron2.exe" (
  echo Falta Instalar-Escuadron2.exe en esta carpeta.
  pause
  exit /b 1
)
set EXTRAS=
if exist "texturas_hd\" set EXTRAS=%EXTRAS% "texturas_hd/GSWE64=texturas_hd"
for %%f in (musica.flac musica.ogg musica.mp3 musica.wav) do if exist "%%f" set EXTRAS=%EXTRAS% "arte/%%f=%%f"
if "%EXTRAS%"=="" (
  echo No hay ni texturas_hd ni musica.* en esta carpeta.
  pause
  exit /b 1
)
echo Armando el instalador (con muchas texturas tarda unos minutos)...
start /wait "" "Instalar-Escuadron2.exe" --agregar "Instalar-Escuadron2-completo.exe" %EXTRAS%
