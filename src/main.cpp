/**
 * NeuralSpeech — FP1 + FP2 : Numérisation et conditionnement du signal audio
 *
 * FP1 (ET1) : ADC 12 bits à Fe = 32 kHz, déclenchement par TC0-CH0.
 *             Restitution DAC0 : signal filtré (pour validation oscilloscope FP2).
 *
 * FP2 (ET2, ET3) :
 *   - Filtre RIF passe-bas 97 taps (Hamming, fc=3577 Hz, fc_design=-6dB)
 *     atténuation >= 30 dB à 4 kHz (ET2).
 *   - Implémenté en arithmétique Q15 (int16/int32) — pas de FPU nécessaire.
 *     Le SAM3X8E (Cortex-M3) n'a pas de FPU hardware : les MAC float
 *     coûtaient ~100-150 cycles chacun via __aeabi_fmul/__aeabi_fadd.
 *     Avec int32 MUL, chaque MAC coûte ~3-5 cycles → gain x30.
 *   - Implémenté dans la loop() via buffer circulaire dédié (pas dans l'ISR).
 *   - Mesure du temps de filtrage via micros() — doit être < 31 µs (ET3).
 *   - Sous-échantillonnage x4 : 1 échantillon filtré sur SUBSAMPLE_FACTOR
 *     poussé dans le buffer audio 8 kHz (pour FP3/FP4).
 *
 * Choix ISR vs loop pour le filtrage :
 *   On filtre dans la loop(), PAS dans l'ISR.
 *   Justification : 97 MAC int32 ≈ 6 µs théoriques << 31.25 µs entre deux TC0.
 *   La loop() consomme tout le buffer ADC avant la prochaine interruption.
 *
 * Board : Arduino Due (SAM3X8E, 84 MHz, 96 Kio SRAM)
 * Framework : PlatformIO + Arduino
 */

#include <Arduino.h>
#include "filter_coefs.h"   // FILTER_TAPS, FILTER_COEFS — généré par design_filter.py

// ---------------------------------------------------------------------------
// Registres DWT Cortex-M3 (Data Watchpoint & Trace — Cycle Counter)
// ---------------------------------------------------------------------------
// Adresses ARM standard, non exposées par le core Arduino-SAM.
// DWT->CYCCNT est un compteur 32 bits qui incrémente à chaque cycle CPU
// (F_CPU = 84 MHz → wrap tous les 51 s). Lecture 1 cycle, zéro overhead.
#define DWT_CTRL     (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT   (*(volatile uint32_t*)0xE0001004)
#define SCB_DEMCR    (*(volatile uint32_t*)0xE000EDFC)
#define DEMCR_TRCENA            (1u << 24)
#define DWT_CTRL_CYCCNTENA      (1u << 0)

// ---------------------------------------------------------------------------
// Constantes de configuration — aucun magic number ailleurs dans le code
// ---------------------------------------------------------------------------

/** Fréquence d'échantillonnage ADC cible en Hz */
constexpr uint32_t FE = 32000;

/** Facteur de sous-échantillonnage : 32 kHz -> 8 kHz */
constexpr uint32_t SUBSAMPLE_FACTOR = 4;

/** Fréquence de sortie après sous-échantillonnage */
constexpr uint32_t FE_OUT = FE / SUBSAMPLE_FACTOR;   // = 8000 Hz

/**
 * Taille du buffer circulaire ADC (puissance de 2 obligatoire).
 * 512 échantillons = 16 ms de signal à 32 kHz.
 */
constexpr uint32_t BUFFER_SIZE      = 512;
constexpr uint32_t BUFFER_MASK      = BUFFER_SIZE - 1;

/**
 * Taille du buffer circulaire 8 kHz (puissance de 2).
 * 2048 échantillons = 256 ms à 8 kHz.
 * Utilisé par FP3 (transfert série) et FP4 (MFCC).
 * Taille en int16 : 2048 × 2 = 4 Kio (SRAM disponible : 96 Kio).
 */
constexpr uint32_t BUF8K_SIZE       = 2048;
constexpr uint32_t BUF8K_MASK       = BUF8K_SIZE - 1;

/**
 * Taille du buffer circulaire interne au filtre RIF.
 * Doit être >= FILTER_TAPS. On prend la puissance de 2 supérieure
 * pour pouvoir utiliser le masque à la place du modulo.
 * FILTER_TAPS = 97 → puissance de 2 supérieure = 128.
 */
constexpr uint32_t FIR_BUF_SIZE     = 128;   // >= FILTER_TAPS (97), puissance de 2
constexpr uint32_t FIR_BUF_MASK     = FIR_BUF_SIZE - 1;

/** Canal ADC correspondant à A0 sur l'Arduino Due (AD7 = canal 7) */
constexpr uint32_t ADC_CHANNEL      = 7;

/** Broche DAC pour la restitution analogique (validation oscilloscope) */
constexpr uint32_t DAC_PIN          = DAC0;

/** Baudrate série */
constexpr uint32_t SERIAL_BAUDRATE  = 250000;

/**
 * Prescaler TC : TIMER_CLOCK2 = MCK/8 = 10 500 000 Hz
 * RC pour 32 kHz : 10 500 000 / 32 000 = 328.125 → 328
 * Fe réelle = 10 500 000 / 328 = 32 012 Hz (erreur < 0.04 %)
 */
constexpr uint32_t TC_RC_VALUE      = 328;

/**
 * Offset DC de l'ADC : 2048 (milieu de gamme 12 bits).
 * On soustrait cette valeur avant filtrage pour centrer le signal autour de 0.
 * Le filtre Q15 travaille en int16 centré [-2048, +2047].
 */
constexpr int16_t ADC_MIDSCALE      = 2048;

/** Nombre d'échantillons sur lesquels on moyennise le temps de filtrage */
constexpr uint32_t TIMING_WINDOW    = 1000;

// ---------------------------------------------------------------------------
// Buffer circulaire ADC (producteur = ISR, consommateur = loop)
// ---------------------------------------------------------------------------

volatile uint16_t adcBuffer[BUFFER_SIZE];
volatile uint32_t bufHead = 0;
volatile uint32_t bufTail = 0;

// ---------------------------------------------------------------------------
// Buffer circulaire 8 kHz (producteur = loop/filtre, consommateur = FP3/FP4)
// ---------------------------------------------------------------------------

/**
 * buf8k stocke les échantillons filtrés et sous-échantillonnés à 8 kHz.
 * Format int16 : plage [-2048, +2047] (12 bits signés, centrés sur 0).
 * Déclaré volatile car il sera lu/écrit depuis différents contextes
 * (ici tout dans la loop, mais déclaration cohérente pour FP3).
 */
volatile int16_t  buf8k[BUF8K_SIZE];
volatile uint32_t buf8kHead = 0;   // producteur (loop — filtrage)
volatile uint32_t buf8kTail = 0;   // consommateur (FP3 — transfert série)

// ---------------------------------------------------------------------------
// Buffer circulaire interne du filtre RIF
// ---------------------------------------------------------------------------

/**
 * firBuf est le registre à décalage du filtre RIF.
 * Type int16_t : échantillons centrés autour de 0 (ADC - 2048), plage [-2048, +2047].
 * firBufHead pointe sur la position où écrire le prochain échantillon.
 * Le calcul de convolution parcourt FILTER_TAPS positions en arrière.
 * Taille = FIR_BUF_SIZE (puissance de 2) pour masque & (N-1).
 */
static int16_t  firBuf[FIR_BUF_SIZE];
static uint32_t firBufHead = 0;

// ---------------------------------------------------------------------------
// Variables de mesure de fréquence réelle (FP1)
// ---------------------------------------------------------------------------

volatile uint32_t sampleCount      = 0;
uint32_t          lastFreqMeasureUs = 0;

// ---------------------------------------------------------------------------
// Variables de mesure du temps de filtrage (FP2 — ET3)
// ---------------------------------------------------------------------------

/**
 * Mesure du temps de filter_sample() sur TIMING_WINDOW échantillons.
 * Accumulation dans la loop, remise à zéro après affichage.
 */
static uint32_t timingCount       = 0;
static uint32_t timingAccumUs     = 0;
static uint32_t timingMaxUs       = 0;

// ---------------------------------------------------------------------------
// Prototypes
// ---------------------------------------------------------------------------

void     configureTC0(void);
void     configureADC(void);
void     configureDAC(void);
int16_t  filter_sample(uint16_t new_sample);

// ---------------------------------------------------------------------------
// ISR : Timer Counter 0 — canal 0
// ---------------------------------------------------------------------------

/**
 * TC0_Handler est appelé à Fe = 32 kHz par TC0-CH0.
 *
 * RÈGLE : ISR ULTRA-COURTE.
 * On fait uniquement :
 *   1. Lecture TC_SR (efface le flag CPCS).
 *   2. Déclenchement + attente conversion ADC (polling ~84 cycles).
 *   3. Push dans adcBuffer.
 *   4. Incrément du compteur de fréquence.
 *
 * Le filtrage est fait dans la loop() — pas ici.
 */
void TC0_Handler(void)
{
    uint32_t status = TC0->TC_CHANNEL[0].TC_SR;
    (void)status;

    ADC->ADC_CR = ADC_CR_START;
    while (!(ADC->ADC_ISR & (1u << ADC_CHANNEL))) {}

    uint16_t sample = (uint16_t)(ADC->ADC_CDR[ADC_CHANNEL] & 0x0FFF);

    adcBuffer[bufHead] = sample;
    bufHead = (bufHead + 1) & BUFFER_MASK;

    sampleCount++;
}

// ---------------------------------------------------------------------------
// Filtre RIF — filter_sample()
// ---------------------------------------------------------------------------

/**
 * Applique le filtre RIF passe-bas (FILTER_TAPS coefficients) à un nouvel
 * échantillon ADC et retourne le résultat filtré en int16_t Q15.
 *
 * Implémentation Q15 (arithmétique entière — pas de FPU) :
 *   - Centrage : centered = (int16_t)new_sample - 2048, plage [-2048, +2047]
 *   - Convolution : acc += (int32_t)FILTER_COEFS_Q15[k] * (int32_t)firBuf[idx]
 *     Chaque coef Q15 est dans [-32768, +32767].
 *     Chaque sample est dans [-2048, +2047].
 *     Produit max : 32767 × 2047 = 67,053,569.
 *     Somme de 97 taps pire cas : 97 × 67M ≈ 6.5G → déborde int32 !
 *     MAIS le filtre est normalisé (gain DC = 1), donc sum(|h[k]|) ≈ 1 en float,
 *     soit sum(|coef_q15[k]|) ≈ 32767. Pire cas réel : 32767 × 2047 = 67M << 2.15G.
 *     Marge constatée : x17.5 (calculée dans design_filter.py).
 *     L'accumulateur int32 ne déborde PAS.
 *   - Sortie : (int16_t)(acc >> 15) — annule le facteur Q15 du coefficient
 *
 * Coût théorique : 97 × ~5 cycles MUL = 485 cycles ≈ 5.8 µs @ 84 MHz.
 * (vs 900 µs avec float emulé — gain x155)
 *
 * @param new_sample  Valeur ADC brute 12 bits (0..4095)
 * @return            Signal filtré int16_t, centré sur 0, plage [-2048, +2047]
 */
int16_t filter_sample(uint16_t new_sample)
{
    // Centrage DC : soustraction du point milieu 12 bits → [-2048, +2047]
    int16_t centered = (int16_t)new_sample - ADC_MIDSCALE;

    // Écriture dans le buffer circulaire RIF
    firBuf[firBufHead] = centered;
    firBufHead = (firBufHead + 1) & FIR_BUF_MASK;

    // Convolution Q15 : y[n] = sum(h_q15[k] * x[n-k], k=0..FILTER_TAPS-1)
    // firBufHead pointe SUR la position APRÈS le dernier écrit,
    // donc x[n] est à (firBufHead - 1) & FIR_BUF_MASK.
    // Accumulateur int32 — voir analyse de dynamique ci-dessus.
    int32_t  acc = 0;
    uint32_t idx = (firBufHead - 1) & FIR_BUF_MASK;  // x[n] = échantillon le plus récent

    for (uint32_t k = 0; k < FILTER_TAPS; k++)
    {
        acc += (int32_t)FILTER_COEFS_Q15[k] * (int32_t)firBuf[idx];
        idx  = (idx - 1) & FIR_BUF_MASK;   // masque puissance-de-2 : pas de modulo
    }

    // Normalisation Q15 : on a multiplié les coefs par 32767, on divise par 2^15
    // Résultat centré sur 0, plage théorique [-2048, +2047] (même que l'entrée)
    return (int16_t)(acc >> 15);
}

// ---------------------------------------------------------------------------
// Configuration du Timer Counter 0 — canal 0
// ---------------------------------------------------------------------------

void configureTC0(void)
{
    PMC->PMC_PCER0 = (1u << ID_TC0);
    TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKDIS;
    TC0->TC_CHANNEL[0].TC_CMR =
        TC_CMR_TCCLKS_TIMER_CLOCK2
        | TC_CMR_WAVE
        | TC_CMR_WAVSEL_UP_RC;
    TC0->TC_CHANNEL[0].TC_RC  = TC_RC_VALUE;
    TC0->TC_CHANNEL[0].TC_IER = TC_IER_CPCS;
    TC0->TC_CHANNEL[0].TC_IDR = ~TC_IER_CPCS;
    NVIC_EnableIRQ(TC0_IRQn);
    NVIC_SetPriority(TC0_IRQn, 0);
    TC0->TC_CHANNEL[0].TC_CCR = TC_CCR_CLKEN | TC_CCR_SWTRG;
}

// ---------------------------------------------------------------------------
// Configuration de l'ADC
// ---------------------------------------------------------------------------

void configureADC(void)
{
    PMC->PMC_PCER1 = (1u << (ID_ADC - 32));
    ADC->ADC_CR    = ADC_CR_SWRST;
    // PRESCAL=1 → ADC_CLK = MCK / ((1+1)*2) = 84/4 = 21 MHz (max safe 22 MHz)
    // Conversion 12 bits = 20 ADC cycles ≈ 0.95 µs (vs 20 µs avec PRESCAL=41).
    // Impact : l'ISR TC0 qui spin-wait sur EOC passe de 20 µs → ~1 µs,
    // libère 60 µs de CPU par groupe de 3 samples → temps filtre mesurable
    // correctement (sinon les interruptions ISR polluaient les mesures DWT).
    ADC->ADC_MR    =
        ADC_MR_PRESCAL(1)
        | ADC_MR_STARTUP_SUT64
        | ADC_MR_TRACKTIM(0)
        | ADC_MR_SETTLING_AST3
        | ADC_MR_TRANSFER(1);
    ADC->ADC_CHER  = (1u << ADC_CHANNEL);
    ADC->ADC_IDR   = 0xFFFFFFFF;
}

// ---------------------------------------------------------------------------
// Configuration du DAC0
// ---------------------------------------------------------------------------

void configureDAC(void)
{
    analogWriteResolution(12);   // résolution DAC = 12 bits (0–4095)
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------

void setup(void)
{
    Serial.begin(SERIAL_BAUDRATE);
    while (!Serial) {}

#if defined(FP1_PURE) && (FP1_PURE == 1)
    Serial.println("=== NeuralSpeech FP1 PURE — ADC 32 kHz + DAC brut (pas de filtre) ===");
    Serial.println("*** Mode validation ET1 : DAC0 restitue le signal ADC brut ***");
    Serial.println("*** Utiliser pour mesure Te oscillo + demo Nyquist.       ***");
#else
    Serial.println("=== NeuralSpeech FP1+FP2 — ADC 32 kHz + filtre RIF + buf 8 kHz ===");
#endif
    Serial.print("FE             : "); Serial.print(FE);              Serial.println(" Hz");
    Serial.print("FE_OUT         : "); Serial.print(FE_OUT);          Serial.println(" Hz");
    Serial.print("FILTER_TAPS    : "); Serial.print(FILTER_TAPS);     Serial.println("");
    Serial.print("SUBSAMPLE      : 1/"); Serial.println(SUBSAMPLE_FACTOR);
    Serial.print("BUF ADC        : "); Serial.print(BUFFER_SIZE);     Serial.println(" samples");
    Serial.print("BUF 8kHz       : "); Serial.print(BUF8K_SIZE);      Serial.println(" samples (int16)");
    Serial.print("FIR buf size   : "); Serial.print(FIR_BUF_SIZE);    Serial.println(" (puissance de 2)");
    Serial.println("Configuration ADC, DAC, TC...");

    // Initialiser le buffer RIF à zéro (silence initial)
    for (uint32_t i = 0; i < FIR_BUF_SIZE; i++) { firBuf[i] = 0; }

    // Activation du DWT Cycle Counter (compteur hardware Cortex-M3)
    // Permet de mesurer la duree de filter_sample() au cycle pres,
    // sans l'overhead de ~30 µs de micros() (qui fait un retry-loop
    // sensible aux interruptions TC0).
    SCB_DEMCR |= DEMCR_TRCENA;       // active trace unit
    DWT_CYCCNT  = 0;                 // reset compteur
    DWT_CTRL   |= DWT_CTRL_CYCCNTENA; // enable cycle counter

    configureADC();
    configureDAC();
    configureTC0();

    lastFreqMeasureUs = micros();
    Serial.println("Demarrage acquisition + filtrage...");
}

// ---------------------------------------------------------------------------
// loop() — filtrage, sous-échantillonnage, DAC, debug
// ---------------------------------------------------------------------------

/**
 * La loop() est le consommateur du buffer ADC et le producteur du buffer 8 kHz.
 *
 * Pour chaque échantillon disponible dans adcBuffer :
 *   1. Mesurer le temps avant filter_sample().
 *   2. Appliquer le filtre RIF → signal filtré float.
 *   3. Écrire le signal filtré sur DAC0 (validation oscilloscope ET2).
 *   4. Sous-échantillonnage /4 : 1 sample sur SUBSAMPLE_FACTOR →
 *      convertir en int16 et pousser dans buf8k.
 *   5. Accumuler les statistiques de timing pour le log [FP2].
 *
 * Pas de delay(). Pas de double().
 */
void loop(void)
{
    // Compteur pour le sous-échantillonnage (persistant entre itérations)
    static uint32_t subsampleCounter = 0;

    // --- Consommation du buffer ADC et filtrage ---

    uint32_t head = bufHead;   // lecture atomique (32 bits ARM)
    uint32_t tail = bufTail;

    while (tail != head)
    {
        uint16_t rawSample = adcBuffer[tail];
        tail = (tail + 1) & BUFFER_MASK;

#if defined(FP1_PURE) && (FP1_PURE == 1)
        // ====================================================================
        // Mode FP1 PURE : pas de filtrage, DAC restitue l'ADC brut.
        // Objectif : validation ET1 oscilloscope (Te = 31.25 µs sur marches
        // DAC) et démo repliement spectral à 17 kHz (alias à 15 kHz visible).
        // ====================================================================
        int16_t filtered = (int16_t)((int32_t)rawSample - (int32_t)ADC_MIDSCALE);
        analogWrite(DAC_PIN, (uint32_t)rawSample);  // DAC = ADC brut direct
#else
        // --- Mesure du temps de filtrage via DWT Cycle Counter (ET3) ---
        // DWT->CYCCNT s'incrémente à chaque cycle CPU (F_CPU = 84 MHz).
        // Lecture en 1 cycle hardware, zéro impact sur les ISR, précision
        // au cycle près. Contrairement à micros() qui prend ~20-30 µs sur
        // Due à cause de son retry-loop sensible aux interruptions.
        uint32_t c0 = DWT_CYCCNT;
        int16_t filtered = filter_sample(rawSample);
        uint32_t c1 = DWT_CYCCNT;

        // Différence de cycles (soustraction non-signée gère le wrap 32 bits)
        uint32_t elapsedCycles = c1 - c0;

        // Conversion cycles → centièmes de µs : (cycles * 100) / 84
        uint32_t elapsedUs_x100 = (elapsedCycles * 100UL) / 84UL;

        timingAccumUs += elapsedUs_x100;
        if (elapsedUs_x100 > timingMaxUs) { timingMaxUs = elapsedUs_x100; }
        timingCount++;

        // --- Restitution DAC0 : signal filtré (validation oscilloscope FP2) ---
        // filtered est centré sur 0, plage [-2048, +2047].
        // DAC attend [0, 4095] → on rajoute l'offset DC 2048.
        // Clamp int32 avant cast pour éviter tout débordement sur signal saturé.
        int32_t dacVal = (int32_t)filtered + (int32_t)ADC_MIDSCALE;
        if (dacVal < 0)    { dacVal = 0;    }
        if (dacVal > 4095) { dacVal = 4095; }
        analogWrite(DAC_PIN, (uint32_t)dacVal);
#endif

        // --- Sous-échantillonnage /4 → buffer 8 kHz ---
        subsampleCounter++;
        if (subsampleCounter >= SUBSAMPLE_FACTOR)
        {
            subsampleCounter = 0;

            // filtered est déjà int16_t centré sur 0 — on l'écrit directement.
            // Clamp défensif : en théorie filter_sample retourne [-2048, +2047]
            // mais une saturation interne peut rarement produire ±32767.
            int32_t s16val = (int32_t)filtered;
            if (s16val < -2048) { s16val = -2048; }
            if (s16val >  2047) { s16val =  2047; }

            buf8k[buf8kHead] = (int16_t)s16val;
            buf8kHead = (buf8kHead + 1) & BUF8K_MASK;
            // Note : pas de protection overflow ici — FP3 doit consommer
            // assez vite. Un overflow écrase silencieusement le plus vieux
            // échantillon (même comportement que buf ADC).
        }
    }

    // Mettre à jour l'index de lecture (une seule écriture volatile à la fin)
    bufTail = tail;

    // --- Mesure de fréquence réelle + log FP1/FP2 (~1 fois/seconde) ---

    uint32_t nowUs = micros();
    if ((nowUs - lastFreqMeasureUs) >= 1000000UL)
    {
        // Lire + reset sampleCount de façon atomique
        NVIC_DisableIRQ(TC0_IRQn);
        uint32_t count = sampleCount;
        sampleCount    = 0;
        NVIC_EnableIRQ(TC0_IRQn);

        uint32_t elapsedUs = nowUs - lastFreqMeasureUs;
        uint32_t feReelle  = (uint32_t)((uint64_t)count * 1000000UL / elapsedUs);
        uint32_t bufOccup  = (bufHead - bufTail) & BUFFER_MASK;

        // Log FP1
        Serial.print("[FP1] Fe_reelle=");
        Serial.print(feReelle);
        Serial.print(" Hz | samples=");
        Serial.print(count);
        Serial.print(" | buf_used=");
        Serial.print(bufOccup);
        Serial.print("/");
        Serial.println(BUFFER_SIZE);

        // Log FP2 (ET3)
        if (timingCount > 0)
        {
            // timingAccumUs et timingMaxUs sont déjà en centièmes de µs
            // (précalcul via DWT Cycle Counter dans la loop)
            uint32_t avgUs_x100 = timingAccumUs / timingCount;
            uint32_t avgInt     = avgUs_x100 / 100;
            uint32_t avgFrac    = avgUs_x100 % 100;

            uint32_t maxUs_x100 = timingMaxUs;
            uint32_t maxInt     = maxUs_x100 / 100;
            uint32_t maxFrac    = maxUs_x100 % 100;

            uint32_t buf8kUsed  = (buf8kHead - buf8kTail) & BUF8K_MASK;

            Serial.print("[FP2] filter_us_avg=");
            Serial.print(avgInt); Serial.print("."); Serial.print(avgFrac);
            Serial.print(" max=");
            Serial.print(maxInt); Serial.print("."); Serial.print(maxFrac);
            Serial.print(" | buf8k_used=");
            Serial.print(buf8kUsed);
            Serial.print("/");
            Serial.print(BUF8K_SIZE);
            Serial.print(" | taps=");
            Serial.println(FILTER_TAPS);

            // Reset des accumulateurs de timing
            timingCount   = 0;
            timingAccumUs = 0;
            timingMaxUs   = 0;
        }

        lastFreqMeasureUs = nowUs;
    }
}
