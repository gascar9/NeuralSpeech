/**
 * NeuralSpeech — FP1 + FP2 : Numérisation et conditionnement du signal audio
 *
 * FP1 (ET1) : ADC 12 bits à Fe = 32 kHz, déclenchement par TC0-CH0.
 *             Restitution DAC0 : signal filtré (pour validation oscilloscope FP2).
 *
 * FP2 (ET2, ET3) :
 *   - Filtre RIF passe-bas 97 taps (Hamming, fc=3577 Hz, fc_design=-6dB)
 *     atténuation >= 30 dB à 4 kHz (ET2).
 *   - Implémenté dans la loop() via buffer circulaire dédié (pas dans l'ISR).
 *   - Mesure du temps de filtrage via micros() — doit être < 31 µs (ET3).
 *   - Sous-échantillonnage x4 : 1 échantillon filtré sur SUBSAMPLE_FACTOR
 *     poussé dans le buffer audio 8 kHz (pour FP3/FP4).
 *
 * Choix ISR vs loop pour le filtrage :
 *   On filtre dans la loop(), PAS dans l'ISR.
 *   Justification : le filtre RIF à 97 taps représente ~97 MAC float.
 *   Même si ~2.3 µs théoriques, on ne veut pas de risque d'ISR longue
 *   (jitter, priorité) ni de double-buffering complexe. La loop() est
 *   assez rapide pour consommer tous les échantillons entre deux ISR
 *   (budget : 31.25 µs entre deux interruptions TC0).
 *
 * Board : Arduino Due (SAM3X8E, 84 MHz, 96 Kio SRAM)
 * Framework : PlatformIO + Arduino
 */

#include <Arduino.h>
#include "filter_coefs.h"   // FILTER_TAPS, FILTER_COEFS — généré par design_filter.py

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
 * Mise à l'échelle ADC -> DAC.
 * ADC : 12 bits = 0..4095. DAC : 12 bits = 0..4095.
 * Le filtre RIF travaille en float normalisé autour de 0 (valeur ADC centrée).
 * Offset DC de l'ADC : 2048 (milieu de gamme 12 bits).
 */
constexpr float ADC_MIDSCALE        = 2048.0f;

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
 * firBufHead pointe sur la position où écrire le prochain échantillon.
 * Le calcul de convolution parcourt FILTER_TAPS positions en arrière.
 * Taille = FIR_BUF_SIZE (puissance de 2) pour masque & (N-1).
 */
static float   firBuf[FIR_BUF_SIZE];
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
float    filter_sample(uint16_t new_sample);

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
 * échantillon ADC et retourne le résultat filtré en float.
 *
 * Implémentation : buffer circulaire de taille FIR_BUF_SIZE (puissance de 2).
 *   - L'échantillon est centré autour de 0 avant filtrage (soustraction du DC).
 *   - L'index firBufHead pointe sur la position la plus récente.
 *   - La convolution parcourt FILTER_TAPS échantillons du plus récent au plus ancien.
 *
 * Complexité : FILTER_TAPS = 97 multiplications-accumulations (MAC) float.
 * Coût théorique : 97 × 2 cycles = 194 cycles ≈ 2.3 µs @ 84 MHz.
 * La mesure réelle (via micros()) est affichée dans le log [FP2] (ET3).
 *
 * @param new_sample  Valeur ADC brute 12 bits (0..4095)
 * @return            Signal filtré en float (centré sur 0, même échelle)
 */
float filter_sample(uint16_t new_sample)
{
    // Centrage DC : on soustrait le point milieu 12 bits
    float centered = (float)new_sample - ADC_MIDSCALE;

    // Écriture dans le buffer circulaire RIF
    firBuf[firBufHead] = centered;
    firBufHead = (firBufHead + 1) & FIR_BUF_MASK;

    // Convolution : y[n] = sum(h[k] * x[n-k], k=0..FILTER_TAPS-1)
    // firBufHead pointe maintenant SUR la position APRÈS le dernier écrit,
    // donc x[n] est à (firBufHead - 1) & FIR_BUF_MASK.
    float acc   = 0.0f;
    uint32_t idx = (firBufHead - 1) & FIR_BUF_MASK;  // x[n] = échantillon le plus récent

    for (uint32_t k = 0; k < FILTER_TAPS; k++)
    {
        acc += FILTER_COEFS[k] * firBuf[idx];
        idx  = (idx - 1) & FIR_BUF_MASK;   // & mask = pas de modulo coûteux
    }

    return acc;
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
    ADC->ADC_MR    =
        ADC_MR_PRESCAL(41)
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

    Serial.println("=== NeuralSpeech FP1+FP2 — ADC 32 kHz + filtre RIF + buf 8 kHz ===");
    Serial.print("FE             : "); Serial.print(FE);              Serial.println(" Hz");
    Serial.print("FE_OUT         : "); Serial.print(FE_OUT);          Serial.println(" Hz");
    Serial.print("FILTER_TAPS    : "); Serial.print(FILTER_TAPS);     Serial.println("");
    Serial.print("SUBSAMPLE      : 1/"); Serial.println(SUBSAMPLE_FACTOR);
    Serial.print("BUF ADC        : "); Serial.print(BUFFER_SIZE);     Serial.println(" samples");
    Serial.print("BUF 8kHz       : "); Serial.print(BUF8K_SIZE);      Serial.println(" samples (int16)");
    Serial.print("FIR buf size   : "); Serial.print(FIR_BUF_SIZE);    Serial.println(" (puissance de 2)");
    Serial.println("Configuration ADC, DAC, TC...");

    // Initialiser le buffer RIF à zéro (silence initial)
    for (uint32_t i = 0; i < FIR_BUF_SIZE; i++) { firBuf[i] = 0.0f; }

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

        // --- Mesure du temps de filtrage (ET3) ---
        uint32_t t0 = micros();
        float filtered = filter_sample(rawSample);
        uint32_t t1 = micros();

        uint32_t elapsed = (t1 >= t0) ? (t1 - t0) : 0u;   // protection wrap-around

        timingAccumUs += elapsed;
        if (elapsed > timingMaxUs) { timingMaxUs = elapsed; }
        timingCount++;

        // --- Restitution DAC0 : signal filtré (validation oscilloscope FP2) ---
        // Reconversion float -> 12 bits : on ajoute le DC puis on sature
        float dacVal = filtered + ADC_MIDSCALE;
        if (dacVal < 0.0f)    { dacVal = 0.0f;    }
        if (dacVal > 4095.0f) { dacVal = 4095.0f; }
        analogWrite(DAC_PIN, (uint32_t)dacVal);

        // --- Sous-échantillonnage /4 → buffer 8 kHz ---
        subsampleCounter++;
        if (subsampleCounter >= SUBSAMPLE_FACTOR)
        {
            subsampleCounter = 0;

            // Conversion float [-2048, +2047] → int16_t
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
            // Calcul en virgule fixe entière pour éviter les float dans le log
            // On multiplie par 100 pour obtenir des centièmes de µs
            uint32_t avgUs_x100 = (timingAccumUs * 100UL) / timingCount;
            uint32_t avgInt     = avgUs_x100 / 100;
            uint32_t avgFrac    = avgUs_x100 % 100;

            uint32_t maxUs_x100 = timingMaxUs * 100UL;
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
