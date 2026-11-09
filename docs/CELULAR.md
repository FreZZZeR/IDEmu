# Celular conectado ao iDrive (2026-11-07)

O que existe: o celular entra pelo Wi-Fi, na mesma rede do Mac, e vira a **tela, o toque, o rotor e os botões** da head unit, e o **GPS do carro** (a posição do celular move o GPS virtual, que o HMI lê). O que não existe: Bluetooth, CarPlay, Android Auto e USB do celular (no iDrive 6 os menus de telefone e Bluetooth do HMI aparecem com a codificação certa, mas nenhum celular pareia: ver "Telefone, Bluetooth e CarPlay no HMI do iDrive 6"). A head unit emulada não tem rádio nem controlador USB; CarPlay e Android Auto precisariam de USB com iAP2/AOA e de licenças da Apple e do Google.

## Como usar

1. Inicie um ambiente. Na aba **Celular** (tela Execução), clique em **Ligar a conexão do celular**.
2. A aba mostra um PIN de 6 dígitos e dois endereços: `http://<IP do Mac>:8766/` (controle) e `https://<IP do Mac>:8767/` (controle e GPS).
3. No celular, abra o endereço e digite o PIN. A página mostra a tela do carro ao vivo (toque e arraste na imagem tocam na tela), o rotor (arraste em círculo, ou ◀ ▶), PUSH, as setas e os botões MENU, BACK, OPTION, CD, RADIO, TEL e NAV.
4. Para o GPS, abra o endereço **HTTPS** e aceite o aviso do certificado (autoassinado, só seu) uma vez: sem HTTPS o navegador do celular bloqueia a localização. Ligue "Usar minha localização como GPS do carro".
5. Desligue na mesma aba quando terminar.

## Segurança

* O servidor principal do IDEmu continua só em `127.0.0.1`. O do celular é outro, ligado à rede local só enquanto você deixar ligado.
* PIN aleatório a cada vez, sessão por cookie (`HttpOnly`, `SameSite=Strict`), bloqueio de 60 s depois de 5 PINs errados por endereço.
* Lista fixa de rotas: `/`, `/login`, `/p/state`, `/p/screen.png`, `/p/input`, `/p/gps`. Nada de SSH, arquivos, coding, logs ou ambientes.
* O tráfego em HTTP não é criptografado: use uma rede em que você confia. Pelo HTTPS o certificado é gerado em `idemu/data/phone/` (chave com permissão 600) e refeito quando o IP do Mac muda.

## Como funciona

* `idemu/phone.py`: `PhoneLink` (dois `ThreadingHTTPServer`, o segundo com TLS), `idemu/web/phone.html` (a página do celular, sem dependências), rotas `GET /api/phone`, `POST /api/phone/start|stop` no servidor local (`docs/API.md`).
* Tela: o celular pede `/p/screen.png?since=<seq>` e o servidor segura a resposta até haver quadro novo (PNG, sem polling rápido).
* Toque: coordenadas normalizadas (0 a 1) viram pixels do display da head unit e chamam `rt.touch`, o mesmo caminho do clique na interface do Mac.
* GPS: cada posição vira `GpsSim.set(lat, lon, speed_kmh, heading, hdop)` no modo manual; o HMI recebe pelo NMEA virtual como com qualquer rota. O iDrive 8 não tem GPS virtual: o celular serve só como controle.

Testes: `tests/test_phone.py` (PIN, bloqueio, rotas, entrada, GPS, HTTPS, certificado).

## Telefone, Bluetooth e CarPlay no HMI do iDrive 6 (2026-11-09)

O celular acima é um controle remoto pelo Wi-Fi; ele não pareia com a head unit. Esta seção é sobre os menus de telefone do próprio HMI (NBT Evo, ambiente `f30-bmw`).

**Por que não apareciam:** a codificação. O perfil do IDEmu não grava `PHONE_TYPE`, e o padrão de fábrica é `no_phone`. O HMI escreve isso no `renderer.log`:
`CHmiCommunicationApplication::initMenus: bIsCodedPhoneMain=0`, `CDevCarBluetooth::init iva_PHONE_MAIN/iva_ADDRESSBOOK not coded, ignoring MOST` e `m_bBluetoothCoded(false)`.
`iva_PHONE_MAIN` é a propriedade de sistema `0x4e59`, que o HMI calcula da codificação.

**Como ligar:** grave a codificação (aba Codificação, ou a API):

```
POST /api/env/f30-bmw/coding/flash {"changes":{"PHONE_TYPE":"internal_phone","BT_MODUL_ON_OFF":1}}
```

Opcionais, testados juntos: `A4A`, `CARPLAY_SWT` e `CONNECTED_DRIVE` (= 1). O log passa a mostrar `bIsCodedPhoneMain=1` e `m_bBluetoothCoded(true)`.

**Verificado na tela (F30, boot completo, 2026-11-09):**
* Comunicação (tecla TEL) mostra **Últimas chamadas**, **Discar número**, **Gerenciar dispositivos móveis** e Personalizar menu (`docs/verif_telefone_comunicacao.png`). Antes só havia Personalizar menu.
* Discar número abre "Nenhum telefone conectado. Para utilizar esta função conecte o telefone por Bluetooth", com **Conectar novo telefone** e **Gerenciar telefone** (`docs/verif_telefone_sem_telefone.png`).
* Conectar novo telefone abre **Conexão por Bluetooth** (instruções de pareamento, `docs/verif_telefone_pareamento.png`). O nome do veículo fica em branco, porque `CUserFriendlyName` (`0x54/0x402`) não é respondido.
* Gerenciar dispositivos móveis abre **Dispositivos móveis**: Conectar novo dispositivo ("Ativando a função…"), "Carregando dados…" e **Configurações**, com a caixa **Bluetooth** marcada e **Informação do Bluetooth** (`docs/verif_telefone_bluetooth.png`).

**O que o IDEmu faz para isso** (`idemu/most.py`, `MostBridge.PHONE_DEVICES`): no carro, os FBlocks de telefone são servidos pelo processo `Connectivity` (`/opt/conn`, 105 MB, com a pilha Bluetooth, o `carplay` e o Wi-Fi). Esse processo fica fora do disco do IDEmu (`builder.OS_EXCLUDE`) e não teria rádio para usar. O V850 virtual registra no boot Bluetooth `0x54/0`, Telephone `0x50/1..4` e GeneralPhoneBook `0x51/0`. Sem esse registro, Últimas chamadas e Discar número não aparecem. Ele responde:

| Propriedade | Payload | Log do HMI |
|---|---|---|
| `0x54/0:0x200` CBluetoothEnable | `u8` = 1 | `Bluetooth enabled` |
| `0x54/0:0xC0B` CBTServiceUnavailable | 2 × `u8` = 0 | `All available` |
| `0x54/0:0xC0D` CBluetoothServiceActivationState | 2 × `u8` = 0 | `New bits: 0x0000` |

Os layouts vêm do decodificador de cada classe (`vtable[6]` de `CPxyBluetooth::C…`). As outras ~35 propriedades assinadas (Telephone `0x253 CCallState`, `0x700 CPhoneConnectionStatus`…) só recebem o ack. `IDEMU_PHONE_DEVICES=0` desliga o registro. Sem `PHONE_TYPE` codificado, o HMI ignora esses blocos.

**O que não funciona, e por quê:**
* **Parear um celular:** não há rádio Bluetooth na VM nem a pilha do `Connectivity`. "Conectar novo dispositivo" fica em "Ativando a função…": testei `0xC0D` com `0000`, `0101`, `0001`, `0100`, `0202`, `0303` e `ffff`, e o HMI aceita todos sem mudar a tela.
* **CarPlay:** com `CARPLAY_SWT` o log mostra `SWTMan init bAnyA4ACoded: true, bCarPlayCoded: true`, mas também `Read from registry: CarPlay false` e `CPEnabled: false`. O CarPlay do NBT Evo é uma ativação de software (FSC/SWT, `SystemFunctionSWT` `0xD1/5`), gravada no registro do HMI. O IDEmu não emula isso. Mesmo com a ativação, faltariam o `carplay`/iAP2 do `/opt/conn` e um iPhone por USB ou Wi-Fi. Nenhuma entrada de CarPlay aparece.
* **ConnectedDrive:** os serviços (BMW Assist, Concierge, Logbook, Teleservices…) exigem `ONLINE_SERVICES` e uma unidade telemática (`TELEMATIC_ECU`: ATM/TCB). O log mostra `ONLINE_SERVICES not coded, ignoring MOST`. Não há unidade telemática no IDEmu.

**Armadilha resolvida (reinício do HMI):** com o telefone codificado, o HMI caía no boot (saída 138, `sloginfo`: `SvcDSI watchdog … enterState -> eWriteDiagnostic`). O primeiro `CIgnitionKey` (`0x05/0:0x224`) dispara a carga do perfil da chave em todos os apps. Isso ocupa o thread principal por 11–15 s (`[SvcMN] 14766 ms processing time … FuncID:224`), e com o telefone codificado o tempo passa do limite do watchdog (~13 s; o f33 sobrevive a 12,2 s). O `-h` do `HmiMain3D` é a vigilância de batimento: com `-h=10000` a sonda "SvcDSI watchdog" é criada com 120 s de *timeout* e o HMI caiu nas 2 de 2 partidas (restaurações de 14,6 e 14,7 s). Com `-h=40000` (480 s), as 3 de 3 partidas sobreviveram (12,2, 14,3 e 14,2 s). A amostra é pequena. Teste de estabilidade (boot completo com o telefone codificado, 2026-11-09): 15 min no ar, sendo 11 com TEL/BACK a cada minuto; uma única execução do HMI (nenhum `genericMain entered` a mais), 0 saídas, 0 `SVC_FatalError`, 0 `invalid message`. Por isso o `env.sh` do shim agora exporta `IDEMU_HMI_HEARTBEAT=40000` (troque com a mesma variável no `gui`/`build`). `miss=20` no `watchdog5-omap` não mudou nada. Depois da primeira queda, o `most` não aceita o novo `RegisterMOSTClient` e todas as reexecuções seguintes falham até reiniciar o ambiente.
