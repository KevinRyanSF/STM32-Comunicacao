/* USER CODE BEGIN Header */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Utility.h"
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#define BUS_PORT        GPIOB
#define CLK_PIN         GPIO_PIN_10
#define DATA_PIN        GPIO_PIN_11

#define BTN_PORT        GPIOE
#define BTN_PIN         GPIO_PIN_4  // Agora usando o PE4!
#define LED_PORT        GPIOA
#define LED_PIN         GPIO_PIN_6

#define TEMPO_BIT       20    // 20us (Tempo de meio pulso de clock)
#define NUM_BITS        8     //

#define MEU_ENDERECO    0x01
#define TARGET_ADDRESS  0x02
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile bool iniciar_envio = false;

// Buffer de 32bits
volatile uint32_t buffer_rx32 = 0;
volatile uint8_t bit_counter = 0;
volatile bool pacote_recebido = false;

volatile uint8_t video_addr = 0;
volatile uint8_t video_dado = 0;
volatile uint8_t video_chk = 0;


uint8_t array_tx[100];
uint8_t array_rx[100];
volatile uint8_t rx_index = 0; // Ponteiro de onde salvar o dado recebido
volatile uint32_t pacotes_com_sucesso = 0;
volatile uint32_t pacotes_com_erro = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */
void envia_pacote(uint8_t addr, uint8_t dado);
int handshake(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void envia_pacote(uint8_t addr, uint8_t dado) {
    // 1. Calcula o Checksum (Soma simples do endereço + dado)
    uint8_t checksum = addr + dado;

    // 2. Calcula a Paridade Par (XOR de todos os bits de Addr e Dado)
    uint8_t paridade = 0;
    uint16_t temp_dados = (addr << 8) | dado;
    for(int i = 0; i < 12; i++) {
        if((temp_dados >> i) & 1) paridade ^= 1;
    }

    // 3. Empacota tudo em uma variável de 32 bits
    uint32_t frame = 0;
    frame |= (0UL)             << 0;  // Start Bit (Sempre 0)
    frame |= (addr & 0x0F)     << 1;  // Endereço (4 bits)
    frame |= (dado & 0xFF)     << 5;  // Dado (8 bits)
    frame |= (paridade & 0x01) << 13; // Paridade (1 bit)
    frame |= (checksum & 0xFF) << 14; // Checksum (8 bits)
    frame |= (1UL)             << 22; // Stop Bit (Sempre 1)

    // 4. Prepara o pino de Clock
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = CLK_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BUS_PORT, &GPIO_InitStruct);

    // 5. Envia os 23 bits
    for(uint8_t i = 0; i < 23; i++) {
        HAL_GPIO_WritePin(BUS_PORT, CLK_PIN, GPIO_PIN_RESET);
        Delay_us(TEMPO_BIT);

        // Lê o bit 'i' do pacote e joga no fio
        if((frame >> i) & 1) HAL_GPIO_WritePin(BUS_PORT, DATA_PIN, GPIO_PIN_SET);
        else                 HAL_GPIO_WritePin(BUS_PORT, DATA_PIN, GPIO_PIN_RESET);
        Delay_us(TEMPO_BIT);

        HAL_GPIO_WritePin(BUS_PORT, CLK_PIN, GPIO_PIN_SET);
        Delay_us(TEMPO_BIT);
    }

    // Libera o barramento e devolve o CLK para Entrada
    HAL_GPIO_WritePin(BUS_PORT, DATA_PIN, GPIO_PIN_SET);
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    HAL_GPIO_Init(BUS_PORT, &GPIO_InitStruct);
}

// ==============================================================
// INTERRUPÇÕES: Botão e Clock
// ==============================================================
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {

    // Gatilho do Botão (Dispara o envio)
	if (GPIO_Pin == BTN_PIN) {
		static uint32_t ultimo_clique = 0; // Guarda o tempo do último clique válido

		// Debounce
		if (HAL_GetTick() - ultimo_clique > 250) {
			iniciar_envio = true;
			ultimo_clique = HAL_GetTick(); // Atualiza o cronômetro
		}
	}

    // Gatilho do Clock (Recebe os dados)
    if (GPIO_Pin == CLK_PIN) {
        // O clock subiu! Lemos o estado do pino de dados imediatamente
		uint8_t bit_lido = (HAL_GPIO_ReadPin(BUS_PORT, DATA_PIN) == GPIO_PIN_SET) ? 1 : 0;

		if (bit_lido) buffer_rx32 |= (1UL << bit_counter);
		else          buffer_rx32 &= ~(1UL << bit_counter);

		bit_counter++;

		if (bit_counter == 23) { // <-- Agora são 23 bits!
			bit_counter = 0;
			pacote_recebido = true;
		}
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();

  /* USER CODE BEGIN 2 */
    Utility_Init();

    // Preenche a nossa lista para envio com dados (0, 1, 2... 99)
	for(int i = 0; i < 100; i++) {
		array_tx[i] = i;
	}

    // Garante que o barramento DATA nasce livre (Open-Drain solto = 1)
    HAL_GPIO_WritePin(BUS_PORT, DATA_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET); // LED Apagado
    /* USER CODE END 2 */

    /* Infinite loop */
	  /* USER CODE BEGIN WHILE */
	  while (1)
	  {
		// ==========================================
		// MODO TRANSMISSÃO (MASTER)
		// ==========================================
		if (iniciar_envio) {
			iniciar_envio = false;

			// Zera os contadores para o vídeo
			pacotes_com_sucesso = 0;
			pacotes_com_erro = 0;

			// 100 pacotes
			for (int i = 0; i < 100; i++) {
				HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
				envia_pacote(TARGET_ADDRESS, array_tx[i]); // Envia o dado da lista
				__HAL_GPIO_EXTI_CLEAR_IT(CLK_PIN);
				HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

				int verifica = handshake();

				if (verifica) {
					pacotes_com_sucesso++;
					HAL_GPIO_TogglePin(LED_PORT, LED_PIN); // Pisca indicando tráfego saudável
				} else {
					pacotes_com_erro++;
				}

				Delay_ms(2); // Pausa de segurança entre pacotes
			}

			// Final nos LEDs
			if (pacotes_com_erro > 0) {
				HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET); // Erro (Aceso)
			} else {
				HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);   // 100% Sucesso (Apagado)
			}
		}

		// ==========================================
		// MODO RECEPÇÃO E VALIDAÇÃO EXTREMA (SLAVE)
		// ==========================================
		if (pacote_recebido) {
			pacote_recebido = false;

			// 1. Desempacota
			uint8_t rx_start = (buffer_rx32 >> 0) & 0x01;
			uint8_t rx_addr  = (buffer_rx32 >> 1) & 0x0F;
			uint8_t rx_dado  = (buffer_rx32 >> 5) & 0xFF;
			uint8_t rx_par   = (buffer_rx32 >> 13) & 0x01;
			uint8_t rx_chk   = (buffer_rx32 >> 14) & 0xFF;
			uint8_t rx_stop  = (buffer_rx32 >> 22) & 0x01;

			video_addr = rx_addr;
			video_dado = rx_dado;
			video_chk = rx_chk;

			buffer_rx32 = 0;

			// 2. Refaz os cálculos matemáticos
			uint8_t calc_chk = rx_addr + rx_dado;
			uint8_t calc_par = 0;
			uint16_t temp_dados = (rx_addr << 8) | rx_dado;
			for(int i = 0; i < 12; i++) {
				if((temp_dados >> i) & 1) calc_par ^= 1;
			}

			// 3. Validação
			if (rx_start == 0 && rx_stop == 1) {
				if (rx_addr == MEU_ENDERECO) {
					if (rx_chk == calc_chk && rx_par == calc_par) {

						// --- DADO ÍNTEGRO! ---
						// Guarda na nossa lista e avança o ponteiro
						array_rx[rx_index] = rx_dado;
						rx_index++;
						if (rx_index >= 100) rx_index = 0; // Proteção para não estourar a memória

						// Ação remota: Pisca o LED
						HAL_GPIO_TogglePin(LED_PORT, LED_PIN);

						// O SLAVE RESPONDE O HANDSHAKE!
						HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
						envia_pacote(TARGET_ADDRESS, 0xCC);
						__HAL_GPIO_EXTI_CLEAR_IT(CLK_PIN);
						HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
					}
				}
			}
		}
		/* USER CODE END WHILE */
	  }

    /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(DATA_PIN_GPIO_Port, DATA_PIN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : BTN_PIN_Pin */
  GPIO_InitStruct.Pin = BTN_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_PIN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LED_PIN_Pin */
  GPIO_InitStruct.Pin = LED_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PIN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_PIN_Pin */
  GPIO_InitStruct.Pin = CLK_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(CLK_PIN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DATA_PIN_Pin */
  GPIO_InitStruct.Pin = DATA_PIN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(DATA_PIN_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
// ==============================================================
// FUNÇÃO: Handshake (Exatamente a sua lógica da aula1)
// ==============================================================
int handshake(void) {
    uint32_t tempo_inicio = 0;

    // Aguarda no máximo ~1000ms
    while (tempo_inicio < 1000) {

        if (pacote_recebido) {
            pacote_recebido = false;

            // Extrai só o que importa: Endereço e Dado
            uint8_t rx_addr  = (buffer_rx32 >> 1) & 0x0F;
            uint8_t rx_dado  = (buffer_rx32 >> 5) & 0xFF;
            buffer_rx32 = 0;

            // Se for para meu endereco e for 0xCC
            if (rx_addr == MEU_ENDERECO && rx_dado == 0xCC) {
                return 1;
            }
        }
        tempo_inicio++;
        Delay_ms(1);
    }

    return 0; // Timeout
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
