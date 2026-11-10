# hello (arquivos do usuário no CIC)

Mostra o caminho completo: arquivo no seu Mac → `/mnt/EFS_RO/hello/` no guest.

```bash
mkdir -p environments/<ambiente>/userfiles/hello
cp examples/cic-hello/run.sh environments/<ambiente>/userfiles/hello/
```

Inicie (ou reinicie) o ambiente CIC e, na aba **SSH** (ou `python3 -m idemu ssh <ambiente>`):

```
/mnt/EFS_RO/hello/run.sh
```

Um app de verdade (binário SH-4A para QNX 6.3.2) entra do mesmo jeito: o bit de execução do arquivo no Mac vale no guest.
Veja "Seus próprios apps no CIC" em `docs/CIC.md`.
