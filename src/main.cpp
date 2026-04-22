/**
 * NeuralSpeech — FP1 : Numérisation du signal audio (ET1)
 *
 * Objectif : ADC 12 bits à Fe = 32 kHz, déclenchement par Timer Counter TC0-CH0,
 *            ISR ultra-courte, buffer circulaire, restitution via DAC0 pour validation
 *            oscilloscope, mesure de fréquence réelle via Serial.
 *
 * Board : Arduino Due (SAM3X8E, 84 MHz, 96 Kio SRAM)
 * Framework : PlatformIO + Arduino
 */

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Constantes de configuration — aucun magic number ailleurs dans le code
// ---------------------------------------------------------------------------

/** Fréquence d'échantillonnage cible en Hz */
constexpr uint32_t FE = 32000;

/**
 * Taille du buffer circulaire ADC (puissance de 2 obligatoire).
 * 512 échantillons = 16 ms de signal à 32 kHz.
 * La SRAM du Due (96 Kio) absorbe facilement plusieurs buffers de cette taille.
 */
constexpr uint32_t BUFFER_SIZE = 512;

/** Masque pour l'arithmétique circulaire — remplace le modulo coûteux */
constexpr uint32_t BUFFER_MASK = BUFFER_SIZE - 1;

/**
 * Canal ADC correspondant à A0 sur l'Arduino Due.
 * A0 est mappé sur AD7 (canal 7) dans le hardware SAM3X8E.
 */
constexpr uint32_t ADC_CHANNEL = 7;       // AD7 = broche A0 du Due

/** Broche DAC pour la restitution analogique (validation oscilloscope) */
constexpr uint32_t DAC_PIN = DAC0;        // broche 66, DAC0 du SAM3X8E

/**
 * Baudrate série — 250000 permet de transférer ~31 octets/sample à 32 kHz,
 * suffisant pour des messages de debug ponctuels (non continus).
 */
constexpr uint32_t SERIAL_BAUDRATE = 250000;

/**
 * Prescaler TC : TIMER_CLOCK2 = MCK/8 = 84 000 000 / 8 = 10 500 000 Hz
 * Valeur RC pour 32 kHz : 10 500 000 / 32 000 = 328.125 → arrondi à 328
 * Fe réelle = 10 500 000 / 328 = 32 012 Hz  (erreur < 0,04 %)
 */
constexpr uint32_t TC_RC_VALUE = 328;

// ---------------------------------------------------------------------------
// Buffer circulaire partagé ISR ↔ loop
// ---------------------------------------------------------------------------

/**
 * Buffer de stockage des échantillons ADC bruts (12 bits, valeurs 0–4095).
 * Déclaré volatile car modifié dans l'ISR et lu dans la loop.
 */
volatile uint16_t adcBuffer[BUFFER_SIZE];

/** Index d'écriture (producteur = ISR) */
volatile uint32_t bufHead = 0;

/** Index de lecture (consommateur = loop) */
volatile uint32_t bufTail = 0;

// ---------------------------------------------------------------------------
// Variables de mesure de la fréquence réelle (debug)
// ---------------------------------------------------------------------------

/** Compteur d'échantillons pour mesurer Fe sur 1 seconde */
volatile uint32_t sampleCount = 0;

/** Timestamp de la dernière mesure de fréquence (en µs) */
uint32_t lastFreqMeasureUs = 0;

// ---------------------------------------------------------------------------
// Prototypes
// ---------------------------------------------------------------------------

void configureTC0(void);
void configureADC(void);
void configureDAC(void);

// ---------------------------------------------------------------------------
// ISR : Timer Counter 0 — canal 0
// ---------------------------------------------------------------------------

/**
 * TC0_Handler est appelé à Fe = 32 kHz par le TC0-CH0.
 *
 * Règle : ISR ultra-courte.
 *  1. Lire le registre de statut du TC (efface le flag d'interruption).
 *  2. Déclencher une conversion ADC manuelle et attendre la fin.
 *  3. Pousser la valeur dans le buffer circulaire.
 *  4. Incrémenter le compteur de fréquence.
 *
 * Durée estimée : lecture ADC (≈20 cycles ADC @ 1 MHz ADC_CLK = 20 µs max)
 * → On configure l'ADC en mode déclenchement logiciel pour que la conversion
 *   se fasse entièrement dans l'ISR sans overhead d'une deuxième interruption.
 *
 * Note : On lit ADC_ISR pour attendre la fin de conversion (polling court,
 * acceptable car la conversion ADC 12 bits dure ~1 µs à 1 MHz ADC_CLK,
 * soit ~84 cycles CPU — bien en dessous du budget de 2600 cycles).
 */
void TC0_Handler(void)
{
    // Lire TC_SR efface automatiquement le flag CPCS (Compare RC Status)
    // Sans cette lecture, l'ISR serait rappelée en boucle infinie.
    uint32_t status = TC0->TC_CHANNEL[0].TC_SR;
    (void)status; // évite le warning "unused variable"

    // Déclencher une conversion ADC sur le canal configuré
    ADC->ADC_CR = ADC_CR_START;

    // Attendre la fin de conversion du canal ADC_CHANNEL
    // ADC_ISR_EOC7 = End Of Conversion canal 7 (A0)
    while (!(ADC->ADC_ISR & (1u << ADC_CHANNEL))) { /* spin court ~84 cycles */ }

    // Lire la valeur 12 bits (bits [11:0] du registre de données)
    uint16_t sample = (uint16_t)(ADC->ADC_CDR[ADC_CHANNEL] & 0x0FFF);

    // Écriture dans le buffer circulaire avec masque (pas de modulo)
    uint32_t nextHead = (bufHead + 1) & BUFFER_MASK;

    // Détecter overflow (buffer plein) : on écrase le plus vieux échantillon
    // plutôt que de bloquer — la loop doit consommer assez vite.
    adcBuffer[bufHead] = sample;
    bufHead = nextHead;

    // Compteur pour la mesure de fréquence réelle
    sampleCount++;
}

// ---------------------------------------------------------------------------
// Configuration du Timer Counter 0 — canal 0
// ---------------------------------------------------------------------------

/**
 * Configure TC0-CH0 en mode Waveform (génération de signal) avec Compare RC.
 * À chaque fois que le compteur atteint RC, il génère une interruption.
 * Cela produit une cadence exacte de Fe = MCK / (prescaler × RC).
 *
 * Registres SAM3X8E utilisés :
 *  - PMC_PCER0 : activation de l'horloge périphérique TC0 (ID = 27)
 *  - TC_CCR : Clock Control Register (enable + reset)
 *  - TC_CMR : Channel Mode Register (prescaler, mode waveform, CPCTRG)
 *  - TC_RC  : valeur de comparaison
 *  - TC_IER : Interrupt Enable Register (CPCS = Compare RC)
 */
void configureTC0(void)
{
    // 1. Activer l'horloge du périphérique TC0 dans le PMC
    //    TC0 = périphérique ID 27 selon le datasheet SAM3X8E p.38
    PMC->PMC_PCER0 = (1u << ID_TC0);

    // 2. Désactiver le clock du canal avant configuration (évite comportement erratique)
    TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKDIS;

    // 3. Configurer le mode du canal
    //    - TCCLKS_TIMER_CLOCK2 : horloge MCK/8 = 10.5 MHz
    //    - WAVE = 1            : mode Waveform (pas Capture)
    //    - WAVSEL_UP_RC        : compteur UP, reset automatique quand compteur = RC
    //    - ACPA/ACPC/etc.      : on ne gère pas la sortie TIOA pour ce projet
    TC0->TC_CHANNEL[0].TC_CMR =
        TC_CMR_TCCLKS_TIMER_CLOCK2   // MCK/8 = 10 500 000 Hz
        | TC_CMR_WAVE                 // mode Waveform
        | TC_CMR_WAVSEL_UP_RC;        // reset sur Compare RC → fréquence fixe

    // 4. Charger la valeur RC : Fe = 10 500 000 / 328 ≈ 32 012 Hz
    TC0->TC_CHANNEL[0].TC_RC = TC_RC_VALUE;

    // 5. Activer l'interruption sur Compare RC (CPCS)
    TC0->TC_CHANNEL[0].TC_IER = TC_IER_CPCS;
    TC0->TC_CHANNEL[0].TC_IDR = ~TC_IER_CPCS; // s'assurer que les autres sont masquées

    // 6. Activer le vecteur d'interruption TC0 dans le NVIC
    NVIC_EnableIRQ(TC0_IRQn);
    NVIC_SetPriority(TC0_IRQn, 0); // priorité maximale pour minimiser la gigue

    // 7. Démarrer le compteur (Software Trigger = reset + enable)
    TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
}

// ---------------------------------------------------------------------------
// Configuration de l'ADC
// ---------------------------------------------------------------------------

/**
 * Configure l'ADC du SAM3X8E pour la conversion sur A0 (canal 7).
 *
 * Paramètres clés :
 *  - Résolution : 12 bits
 *  - ADC_CLK cible : ~1 MHz (bien en dessous du max de 20 MHz)
 *    Prescaler = (MCK / (2 × ADC_CLK)) - 1 = (84 000 000 / 2 000 000) - 1 = 41
 *  - Mode : déclenchement logiciel (ADC_MR_TRGEN_DIS = pas de trigger hardware)
 *    Le déclenchement est fait manuellement dans l'ISR via ADC_CR_START.
 *  - Startup time : ADC_MR_STARTUP_SUT64 (64 périodes ADC_CLK)
 *  - Tracking time : ADC_MR_TRACKTIM minimum (0) — signal stable venant du MAX9814
 */
void configureADC(void)
{
    // 1. Activer l'horloge du périphérique ADC (ID = 37 selon datasheet)
    PMC->PMC_PCER1 = (1u << (ID_ADC - 32));

    // 2. Reset de l'ADC
    ADC->ADC_CR = ADC_CR_SWRST;

    // 3. Configurer le Mode Register
    //    Prescaler : ADC_CLK = MCK / (2 × (PRESCAL + 1))
    //    Pour ADC_CLK ≈ 1 MHz : PRESCAL = 84/2 - 1 = 41
    ADC->ADC_MR =
        ADC_MR_PRESCAL(41)            // ADC_CLK = 84 MHz / (2×42) ≈ 1 MHz
        | ADC_MR_STARTUP_SUT64        // 64 cycles de startup (standard)
        | ADC_MR_TRACKTIM(0)          // tracking time minimal (0+1 cycles)
        | ADC_MR_SETTLING_AST3        // settling time 3 cycles
        | ADC_MR_TRANSFER(1);         // transfer time 1+1 cycles

    // Déclenchement logiciel (TRGEN = 0 par défaut = software trigger)
    // Pas d'FREERUN — on contrôle chaque conversion depuis l'ISR du TC.

    // 4. Activer uniquement le canal 7 (A0)
    ADC->ADC_CHER = (1u << ADC_CHANNEL);

    // 5. Désactiver toutes les interruptions ADC — on fait du polling dans l'ISR TC
    ADC->ADC_IDR = 0xFFFFFFFF;
}

// ---------------------------------------------------------------------------
// Configuration du DAC0 (validation oscilloscope)
// ---------------------------------------------------------------------------

/**
 * Configure DAC0 pour la restitution du signal numérisé.
 * Le DAC du SAM3X8E est 12 bits, sortie sur la broche DAC0 (broche 66 du Due).
 *
 * On utilise analogWrite() d'Arduino Due qui configure le DAC en 12 bits
 * automatiquement si on appelle analogWriteResolution(12) en setup().
 */
void configureDAC(void)
{
    analogWriteResolution(12); // résolution DAC = 12 bits (0–4095)
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------

void setup(void)
{
    // Initialisation de la liaison série pour le debug
    Serial.begin(SERIAL_BAUDRATE);
    while (!Serial) { /* attendre que le port soit prêt */ }

    Serial.println("=== NeuralSpeech FP1 — ADC 32 kHz ===");
    Serial.print("Fe cible       : "); Serial.print(FE);         Serial.println(" Hz");
    Serial.print("Fe reelle      : "); Serial.print(10500000UL / TC_RC_VALUE); Serial.println(" Hz");
    Serial.print("Buffer size    : "); Serial.print(BUFFER_SIZE); Serial.println(" samples");
    Serial.println("Configuration ADC, TC, DAC...");

    // Configurer les périphériques dans l'ordre :
    // 1. ADC (avant de démarrer le TC pour éviter une conversion sans canal actif)
    configureADC();

    // 2. DAC (pour la restitution oscilloscope)
    configureDAC();

    // 3. TC0 (démarre le timer → les ISR commencent à arriver)
    configureTC0();

    // Timestamp de référence pour la mesure de fréquence
    lastFreqMeasureUs = micros();

    Serial.println("Demarrage acquisition...");
}

// ---------------------------------------------------------------------------
// loop() — consommateur du buffer, restitution DAC, debug fréquence
// ---------------------------------------------------------------------------

/**
 * La loop() lit les échantillons disponibles dans le buffer circulaire et les
 * écrit sur DAC0 pour permettre la validation oscilloscope (ET1).
 *
 * Elle mesure aussi la fréquence réelle d'échantillonnage environ 1 fois/seconde
 * en comptant les échantillons produits par l'ISR.
 *
 * IMPORTANT : pas de delay(), pas de Serial dans le chemin critique.
 * Le Serial.print de debug est déclenché seulement toutes les ~8000 itérations,
 * ce qui correspond à ~1 seconde à 32 kHz.
 */
void loop(void)
{
    // --- Consommation du buffer et restitution DAC ---

    // Lire les indices de façon atomique (uint32_t, lecture 32 bits = atomique sur ARM)
    uint32_t head = bufHead;
    uint32_t tail = bufTail;

    while (tail != head)
    {
        // Lire l'échantillon 12 bits depuis le buffer circulaire
        uint16_t sample = adcBuffer[tail];

        // Avancer l'index de lecture avec masque (pas de modulo)
        tail = (tail + 1) & BUFFER_MASK;

        // Écrire sur DAC0 : la tension de sortie reproduit le signal ADC
        // Le DAC du Due accepte des valeurs 0–4095 en résolution 12 bits
        analogWrite(DAC_PIN, sample);
    }

    // Mettre à jour l'index de lecture (une seule écriture volatile à la fin)
    bufTail = tail;

    // --- Mesure de la fréquence réelle (~1 fois par seconde) ---

    uint32_t nowUs = micros();

    // Déclencher la mesure toutes les secondes environ (1 000 000 µs)
    if ((nowUs - lastFreqMeasureUs) >= 1000000UL)
    {
        // Lire et remettre à zéro le compteur d'échantillons de façon atomique
        // On désactive brièvement l'interruption TC0 pour éviter une race condition
        NVIC_DisableIRQ(TC0_IRQn);
        uint32_t count = sampleCount;
        sampleCount = 0;
        NVIC_EnableIRQ(TC0_IRQn);

        // Calculer la fréquence réelle en Hz sur l'intervalle écoulé
        uint32_t elapsedUs = nowUs - lastFreqMeasureUs;
        uint32_t feReelle  = (uint32_t)((uint64_t)count * 1000000UL / elapsedUs);

        // Calcul du niveau d'occupation du buffer (pour détecter un overflow)
        uint32_t occupation = (bufHead - bufTail) & BUFFER_MASK;

        // Affichage debug — une seule ligne par seconde, compact pour 250 kBaud
        Serial.print("[FP1] Fe_reelle=");
        Serial.print(feReelle);
        Serial.print(" Hz | samples=");
        Serial.print(count);
        Serial.print(" | buf_used=");
        Serial.print(occupation);
        Serial.print("/");
        Serial.println(BUFFER_SIZE);

        lastFreqMeasureUs = nowUs;
    }
}
