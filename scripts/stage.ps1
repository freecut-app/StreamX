# Monta a pasta de execução do StreamX (rundir) a partir da instalação do libobs.
# Copia o motor, os plugins e as dependências; exclui a interface do OBS Studio.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root 'deps\obs-install'
$dst = Join-Path $root 'rundir'
if (-not (Test-Path $src)) { throw "Rode antes: cmake --install deps/obs-studio/build_streamx --prefix deps/obs-install" }

# Itens que pertencem ao frontend do OBS Studio e não são usados pelo StreamX
$excluir = @('obs64.exe', 'obs64.pdb', 'obs-frontend-api.dll', 'obs-frontend-api.pdb',
             'frontend-tools.dll', 'frontend-tools.pdb', 'obs-websocket.dll', 'obs-websocket.pdb')
$excluirDirs = @('data\obs-studio', 'data\obs-plugins\frontend-tools', 'data\obs-plugins\obs-websocket')

New-Item -ItemType Directory -Force $dst | Out-Null
robocopy $src $dst /E /NFL /NDL /NJH /NJS /NP /XF $excluir /XD ($excluirDirs | ForEach-Object { Join-Path $src $_ }) | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy falhou ($LASTEXITCODE)" }
# Remove arquivos de depuração para deixar o pacote menor (mantidos na build)
Get-ChildItem $dst -Recurse -Filter *.pdb | Remove-Item -Force
Write-Host "rundir pronto em $dst"
