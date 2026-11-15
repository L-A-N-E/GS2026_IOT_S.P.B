@echo off
rem S.B.P. - abre o painel pelo servidor local (mapa completo + GPS do Olho Vivo)
cd /d "%~dp0"
where py >nul 2>nul && (py servidor_sbp.py & goto :fim)
where python >nul 2>nul && (python servidor_sbp.py & goto :fim)
echo Python nao encontrado. Instale em https://www.python.org/downloads/ (marque "Add to PATH").
:fim
pause
