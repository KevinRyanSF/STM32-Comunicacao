# Protocolo de Comunicação Serial Customizado para STM32

**Demonstração Prática:** [Assistir ao Vídeo no YouTube](https://youtu.be/93XQf1JEAgQ)

## Visão Geral
Este repositório contém a implementação de um protocolo de comunicação serial síncrona e half-duplex, desenvolvido em linguagem C para a família de microcontroladores STM32F4 utilizando o ambiente STM32CubeIDE. 

O projeto foi projetado para demonstrar o controle de baixo nível da camada física, roteamento de sinais baseados em eventos (interrupções) e encapsulamento de dados com técnicas de validação de integridade para ambientes sujeitos a ruído elétrico.

## Especificações da Camada Física
A comunicação ocorre através de uma topologia de barramento composta por dois fios de sinal e uma referência comum:
* **CLK (Clock):** Pino PB10
* **DATA (Dados):** Pino PB11
* **Referência:** GND

Para garantir a segurança elétrica e prevenir curtos-circuitos durante colisões de barramento, as portas GPIO (General Purpose Input/Output) operam em modo **Open-Drain** acopladas a resistores de **Pull-up** internos. O barramento repousa em nível lógico ALTO (3.3V). A transmissão de um bit "0" ocorre pelo acionamento do transistor interno, direcionando a linha para o GND.

## Formato do Frame de Dados
A fim de garantir a segurança da informação, os dados são empacotados em um frame de **23 bits**, transmitidos no formato LSB First (Bit menos significativo primeiro).

| Bit(s)   | Tamanho | Campo         | Descrição                                                                 |
|----------|---------|---------------|---------------------------------------------------------------------------|
| 0        | 1 bit   | Start Bit     | Fixo em `0`. Inicia a transmissão tirando a linha do estado ocioso.       |
| 1 a 4    | 4 bits  | Endereço      | Identificador do nó de destino da mensagem.                               |
| 5 a 12   | 8 bits  | Payload (Dado)| Carga útil transportando a instrução ou valor numérico.                   |
| 13       | 1 bit   | Paridade      | Paridade Par calculada via operação XOR entre os bits de Endereço e Dado. |
| 14 a 21  | 8 bits  | Checksum      | Soma aritmética de verificação (Endereço + Dado).                         |
| 22       | 1 bit   | Stop Bit      | Fixo em `1`. Encerra o frame e libera o barramento.                       |

## Sincronismo e Diagrama de Tempo
O protocolo opera de forma síncrona, onde o nó Mestre (Transmissor) gera o sinal de Clock. A captura do bit ocorre de forma orientada a interrupções (EXTI) no nó Escravo (Receptor). A temporização ocorre em três etapas fundamentais:

1. **Setup:** O transmissor impõe nível BAIXO na linha `CLK` e define o valor lógico na linha `DATA`.
2. **Amostragem (Hold):** Após o tempo de estabilização, o transmissor libera a linha `CLK` para nível ALTO. Esta borda de subida gera uma interrupção no receptor, que lê o estado da linha `DATA`.
3. **Finalização:** O transmissor encerra o ciclo do pulso e prepara o sistema para o próximo bit.

## Máquina de Estados e Fluxo da Aplicação

### Modo Transmissor (Mestre)
O evento de transmissão é iniciado pelo acionamento do botão físico (K0), que possui um filtro de software para eliminação de ruído mecânico (Debounce de 250ms). O Mestre empacota os dados de um array de 100 posições e transmite cada frame sequencialmente. Após cada envio, a máquina de estados aguarda um pacote de confirmação (Handshake - `0xCC`). Caso o timeout de 100ms seja atingido sem resposta, um erro é contabilizado e reportado na interface em hardware (LED de falha).

### Modo Receptor (Escravo)
O receptor opera de forma reativa. Ao detectar atividade no barramento, a interrupção acumula os estados elétricos até o fechamento de 23 ciclos de clock. O frame passa por uma triagem de validação (Start/Stop Bits, Endereço de Destino, Checksum e Paridade). Sendo um frame válido, a carga útil é armazenada na memória, uma instrução remota é executada (alternância de estado do LED) e a placa assume momentaneamente a linha para transmitir o frame de confirmação (`0xCC`) de volta ao Mestre.

## Instruções de Uso e Replicação

### Requisitos
* 2x Placas de Desenvolvimento STM32F4.
* Jumpers para interligação das linhas (GND, PB10 e PB11).
* STM32CubeIDE.

### Configuração
O mesmo código-fonte é executado em ambas as placas. Para diferenciar os nós na rede, altere as diretivas de compilação no cabeçalho do arquivo `main.c` antes da gravação:

**Para a Placa 1 (Nó A):**
```c
#define MEU_ENDERECO    0x01
#define TARGET_ADDRESS  0x02
