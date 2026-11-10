#!/bin/ksh
# Exemplo mínimo de "app" do usuário no CIC: prova que os arquivos da pasta userfiles/ chegaram ao guest e que dá para rodá-los.
# Copie esta pasta para <IDEmu>/environments/<ambiente>/userfiles/hello/ e reinicie o ambiente. Dentro do guest (SSH ou aba SSH do app):
#     /mnt/EFS_RO/hello/run.sh
echo "ola do CIC: $(uname -sr) / $(uname -m)"
echo "arquivos em /mnt/EFS_RO/hello:"
ls -l /mnt/EFS_RO/hello
echo "processos do HMI:"
pidin -f an 2>/dev/null | grep -i "Hmi" | head -5
