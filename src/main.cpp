/**
 * NeuralSpeech — FP1 + FP2 + FP3 : Numérisation, conditionnement, enregistrement
 *
 * FP1 (ET1) : ADC 12 bits à Fe = 32 kHz, déclenchement par TC0-CH0.
 *             Restitution DAC0 : signal brut, DAC1 : signal filtré.
 *
 * FP2 (ET2, ET3) :
 *   - Filtre RIF passe-bas 40 taps (Parks-McClellan, fc_stop=4 kHz)
 *     atténuation >= 30 dB à 4 kHz (ET2, marge mesurée -41.5 dB).
 *   - Implémenté en arithmétique Q15 (int16/int32) — pas de FPU nécessaire.
 *     Le SAM3X8E (Cortex-M3) n'a pas de FPU hardware : les MAC float
 *     coûtaient ~100-150 cycles chacun via __aeabi_fmul/__aeabi_fadd.
 *     Avec int32 MUL, chaque MAC coûte ~3-5 cycles → gain x30.
 *   - Implémenté dans la loop() via buffer circulaire dédié (pas dans l'ISR).
 *   - Mesure du temps de filtrage via DWT Cycle Counter — doit être < 31 µs (ET3).
 *   - Sous-échantillonnage x4 : 1 échantillon filtré sur SUBSAMPLE_FACTOR
 *     poussé dans le buffer audio 8 kHz (pour FP3/FP4).
 *
 * FP3 (ET4) : Enregistrement 3 secondes à 8 kHz, dump binaire sur série.
 *   - Bouton sur D2 (INPUT_PULLUP, front descendant, anti-rebond 50 ms).
 *   - State machine : IDLE → ARMING (capture 24000 samples = 3 s) → DUMPING → IDLE.
 *   - Protocole série : magic header 0xAA55AA55 | uint32 nb_samples |
 *     24000×int16 little-endian | magic footer 0xDEADBEEF.
 *   - Script Python côté PC reconstruit un .wav importable Audacity.
 *
 * Choix ISR vs loop pour le filtrage :
 *   On filtre dans la loop(), PAS dans l'ISR.
 *   Justification : 40 MAC int32 ≈ 2.4 µs théoriques << 31.25 µs entre deux TC0.
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

/**
 * Broches DAC pour la restitution analogique (validation oscilloscope).
 * - DAC0 = signal BRUT (ADC direct, avant filtre) → référence pour comparaison
 * - DAC1 = signal FILTRÉ (après RIF passe-bas FP2) → validation ET2 visuelle
 *
 * Avec les 2 DAC branchés sur CH1/CH2 de l'oscillo, l'efficacité du filtre
 * est démontrée en temps réel : injecter un signal > 4 kHz, DAC0 reste
 * intact, DAC1 s'effondre.
 *
 * En mode FP1_PURE (env due_fp1), seul DAC0 est utilisé.
 */
constexpr uint32_t DAC_RAW_PIN      = DAC0;   // signal ADC brut
constexpr uint32_t DAC_FILTERED_PIN = DAC1;   // signal filtré après FP2

/** Baudrate série */
constexpr uint32_t SERIAL_BAUDRATE  = 250000;

// ---------------------------------------------------------------------------
// Constantes FP3 (ET4)
// ---------------------------------------------------------------------------

/** Broche du bouton poussoir (INPUT_PULLUP → appui = LOW) */
constexpr uint8_t  FP3_BUTTON_PIN        = 2;

/** Durée anti-rebond bouton en millisecondes */
constexpr uint32_t FP3_DEBOUNCE_MS       = 50;

/** Nombre d'échantillons à capturer : 3 s × 8000 Hz = 24000 samples = 48 Kio */
constexpr uint32_t FP3_CAPTURE_SAMPLES   = 24000;

/**
 * Magic header/footer du protocole série binaire FP3.
 * Le parser Python se base sur ces 4 octets pour délimiter le bloc.
 * Indépendants des messages texte [FP1]/[FP2] qui peuvent apparaître
 * pendant le transfert (le parser cherche les magic bytes, pas le silence).
 */
constexpr uint8_t FP3_MAGIC_HEADER[4]   = { 0xAA, 0x55, 0xAA, 0x55 };
constexpr uint8_t FP3_MAGIC_FOOTER[4]   = { 0xDE, 0xAD, 0xBE, 0xEF };

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
// FP3 — State machine, buffer de capture, état bouton
// ---------------------------------------------------------------------------

#if !defined(FP1_PURE) || (FP1_PURE == 0)

/**
 * États de la machine d'état FP3.
 *   FP3_IDLE    : en attente d'appui bouton.
 *   FP3_ARMING  : accumulation de FP3_CAPTURE_SAMPLES dans captureBuffer.
 *   FP3_DUMPING : envoi du contenu via Serial (non-bloquant).
 */
enum Fp3State : uint8_t {
    FP3_IDLE    = 0,
    FP3_ARMING  = 1,
    FP3_DUMPING = 2
};

static volatile Fp3State fp3State = FP3_IDLE;

/**
 * Buffer de capture FP3 : snapshot linéaire de 3 secondes à 8 kHz.
 * Taille : 8000 × 2 octets = 16 000 octets = ~15.6 Kio.
 * Déclaré static (durée de vie = durée du programme).
 * Non-volatile : rempli uniquement depuis la loop() (pas d'ISR).
 */
static int16_t captureBuffer[FP3_CAPTURE_SAMPLES];

/** Nombre d'échantillons déjà écrits dans captureBuffer (état ARMING) */
static uint32_t captureIdx = 0;

/**
 * Index d'envoi dans captureBuffer (état DUMPING).
 * On garde la progression entre deux appels de loop() pour
 * ne pas bloquer (Serial.availableForWrite() check).
 */
static uint32_t dumpIdx = 0;

/**
 * Phase interne du DUMPING :
 *   0 = en train d'envoyer le header (4 octets magic + 4 octets nb_samples)
 *   1 = en train d'envoyer les données PCM (16000 octets)
 *   2 = en train d'envoyer le footer (4 octets)
 * On avance octet par octet pour rester non-bloquant.
 */
static uint8_t  dumpPhase = 0;
static uint32_t dumpPhaseIdx = 0;   // offset dans la phase courante

/** Timestamp du dernier changement d'état du bouton (anti-rebond) */
static uint32_t buttonLastChangeMs = 0;

/** Dernier état lu sur le pin bouton (HIGH=relâché, LOW=pressé) */
static uint8_t  buttonLastState = HIGH;

#endif  // !FP1_PURE

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

#if !defined(FP1_PURE) || (FP1_PURE == 0)
void     fp3_check_button(void);
void     fp3_push_sample(int16_t sample);
void     fp3_service_dump(void);
const char* fp3_state_str(void);
#endif

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
// FP3 — fonctions de la state machine (compilées seulement en mode FP1+FP2)
// ---------------------------------------------------------------------------

#if !defined(FP1_PURE) || (FP1_PURE == 0)

/**
 * fp3_state_str() — retourne le nom textuel de l'état courant.
 * Utilisé dans le log [FP2] pour la colonne fp3=...
 */
const char* fp3_state_str(void)
{
    switch (fp3State)
    {
        case FP3_IDLE:    return "IDLE";
        case FP3_ARMING:  return "ARMING";
        case FP3_DUMPING: return "DUMPING";
        default:          return "???";
    }
}

/**
 * fp3_check_button() — détection front descendant avec anti-rebond.
 *
 * Appelée depuis la loop() à chaque itération (coût : 1 digitalRead +
 * 1 millis() + quelques comparaisons).
 *
 * Logique :
 *   - On lit l'état courant du pin.
 *   - Si l'état a changé ET que 50 ms se sont écoulées depuis le dernier
 *     changement → on valide le nouvel état.
 *   - Un front descendant validé (HIGH→LOW) en état IDLE déclenche ARMING.
 *
 * Pièges :
 *   - Ne PAS utiliser attachInterrupt() pour le bouton : un appui génère
 *     facilement 5-20 rebonds en 1-5 ms, ce qui déclencherait plusieurs
 *     ARMING consécutifs avant que la loop() ait le temps de progresser.
 *   - Le polling dans la loop() + délai 50 ms est plus robuste ici.
 */
void fp3_check_button(void)
{
    // Ne traiter le bouton qu'en IDLE (évite une re-arm accidentelle)
    if (fp3State != FP3_IDLE) { return; }

    uint8_t  currentState = (uint8_t)digitalRead(FP3_BUTTON_PIN);
    uint32_t nowMs        = millis();

    if (currentState != buttonLastState)
    {
        // Changement d'état détecté → démarrer ou remettre à zéro le timer
        // anti-rebond (on accepte le nouvel état seulement si stable 50 ms)
        buttonLastChangeMs = nowMs;
        buttonLastState    = currentState;
    }
    else if ((nowMs - buttonLastChangeMs) >= FP3_DEBOUNCE_MS)
    {
        // État stable depuis >= 50 ms
        if (currentState == LOW)
        {
            // Front descendant confirmé → armement de la capture
            Serial.println("\n[FP3] Capture armee -- parlez maintenant (3 s)");

            // Aligner le consommateur buf8k sur le producteur pour éviter
            // de capturer des échantillons "stale" antérieurs à l'appui.
            // Désactivation IRQ courte pour lecture atomique de buf8kHead.
            NVIC_DisableIRQ(TC0_IRQn);
            buf8kTail = buf8kHead;
            NVIC_EnableIRQ(TC0_IRQn);

            captureIdx = 0;
            fp3State   = FP3_ARMING;
        }
    }
}

/**
 * fp3_push_sample() — appelée depuis la loop() après chaque sample 8 kHz.
 *
 * En état ARMING, copie le sample dans captureBuffer.
 * Quand FP3_CAPTURE_SAMPLES sont accumulés, bascule en DUMPING.
 *
 * Ne doit PAS être appelée depuis l'ISR — la copie mémoire serait trop longue.
 *
 * @param sample  Échantillon int16_t centré sur 0, produit par filter_sample().
 */
void fp3_push_sample(int16_t sample)
{
    if (fp3State != FP3_ARMING) { return; }

    captureBuffer[captureIdx] = sample;
    captureIdx++;

    if (captureIdx >= FP3_CAPTURE_SAMPLES)
    {
        // 3 secondes capturées → prépare le dump série
        dumpIdx      = 0;
        dumpPhase    = 0;
        dumpPhaseIdx = 0;

        // Flush synchrone du texte avant de commencer le binaire : évite que
        // le bridge USB-UART ATmega16U2 mélange caractères ASCII et octets
        // binaires de façon instable sous forte charge.
        Serial.println("\n[FP3] --- DEBUT CAPTURE WAV ---");
        Serial.flush();   // attend que le texte soit entièrement émis
        delay(20);        // pause défensive pour que le bridge respire

        fp3State = FP3_DUMPING;
    }
}

/**
 * fp3_service_dump() — moteur d'envoi non-bloquant, appelé chaque loop().
 *
 * Principe : on n'envoie que ce que le buffer TX série peut accepter
 * MAINTENANT (Serial.availableForWrite()), puis on rend la main.
 * La loop() revient très vite (plusieurs milliers de fois par seconde),
 * donc le débit effectif atteint la limite physique du baudrate sans
 * bloquer la chaîne FP1/FP2.
 *
 * Structure du protocole (ordre strict) :
 *   Phase 0 : 4 octets magic header (0xAA 0x55 0xAA 0x55)
 *             + 4 octets nb_samples little-endian uint32 (= 8000 = 0x1F40)
 *   Phase 1 : 16000 octets de données PCM (8000 × int16 little-endian)
 *   Phase 2 : 4 octets magic footer (0xDE 0xAD 0xBE 0xEF)
 *
 * Pièges :
 *   - Serial.write() sur Due retourne le nombre d'octets réellement écrits.
 *     Si availableForWrite() == 0, Serial.write() peut bloquer ou perdre
 *     des octets selon la version du core. On teste AVANT d'écrire.
 *   - dumpPhaseIdx est remis à 0 à chaque changement de phase.
 *   - Les messages texte [FP1]/[FP2] peuvent s'intercaler dans le flux série
 *     pendant DUMPING : le parser Python doit chercher les magic bytes dans
 *     le flux brut, pas se fier à la propreté du flux texte.
 */
void fp3_service_dump(void)
{
    if (fp3State != FP3_DUMPING) { return; }

    // On boucle tant qu'il reste des octets à envoyer ET que le buffer TX
    // série a de la place. On sort dès que l'un des deux est épuisé.
    while (true)
    {
        if (Serial.availableForWrite() == 0) { return; }   // buffer TX plein → on repasse

        if (dumpPhase == 0)
        {
            // --- Phase 0 : header 8 octets ---
            // Octets 0-3 : magic header
            // Octets 4-7 : nb_samples (uint32 little-endian)
            const uint32_t nbSamples = FP3_CAPTURE_SAMPLES;
            uint8_t headerBuf[8];
            headerBuf[0] = FP3_MAGIC_HEADER[0];
            headerBuf[1] = FP3_MAGIC_HEADER[1];
            headerBuf[2] = FP3_MAGIC_HEADER[2];
            headerBuf[3] = FP3_MAGIC_HEADER[3];
            headerBuf[4] = (uint8_t)( nbSamples        & 0xFF);
            headerBuf[5] = (uint8_t)((nbSamples >>  8) & 0xFF);
            headerBuf[6] = (uint8_t)((nbSamples >> 16) & 0xFF);
            headerBuf[7] = (uint8_t)((nbSamples >> 24) & 0xFF);

            Serial.write(headerBuf[dumpPhaseIdx]);
            dumpPhaseIdx++;

            if (dumpPhaseIdx >= 8)
            {
                dumpPhase    = 1;
                dumpPhaseIdx = 0;
                dumpIdx      = 0;
            }
        }
        else if (dumpPhase == 1)
        {
            // --- Phase 1 : données PCM little-endian ---
            // Chaque int16_t envoyé en 2 octets : octet bas en premier.
            int16_t s = captureBuffer[dumpIdx];

            if (dumpPhaseIdx == 0)
            {
                Serial.write((uint8_t)(s & 0xFF));         // octet bas
                dumpPhaseIdx = 1;
            }
            else
            {
                Serial.write((uint8_t)((s >> 8) & 0xFF));  // octet haut
                dumpPhaseIdx = 0;
                dumpIdx++;

                if (dumpIdx >= FP3_CAPTURE_SAMPLES)
                {
                    dumpPhase    = 2;
                    dumpPhaseIdx = 0;
                }
            }
        }
        else if (dumpPhase == 2)
        {
            // --- Phase 2 : footer 4 octets ---
            Serial.write(FP3_MAGIC_FOOTER[dumpPhaseIdx]);
            dumpPhaseIdx++;

            if (dumpPhaseIdx >= 4)
            {
                // Dump terminé
                Serial.println("\n[FP3] --- FIN CAPTURE WAV ---");
                Serial.println("[FP3] Pret pour la prochaine capture (appuyer D2).");
                fp3State = FP3_IDLE;
                return;
            }
        }
        else
        {
            // État incohérent — repasse en IDLE par sécurité
            fp3State = FP3_IDLE;
            return;
        }
    }
}

#endif  // !FP1_PURE

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
    Serial.println("=== NeuralSpeech FP1+FP2+FP3 — ADC 32 kHz + filtre RIF + enregistrement ===");
    Serial.println("*** DAC0 = signal BRUT (avant filtre)  --> CH1 oscillo ***");
    Serial.println("*** DAC1 = signal FILTRE (apres FP2)   --> CH2 oscillo ***");
    Serial.println("*** D2   = bouton enregistrement FP3 (INPUT_PULLUP)    ***");
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

#if !defined(FP1_PURE) || (FP1_PURE == 0)
    // --- Configuration bouton FP3 ---
    // INPUT_PULLUP : résistance pull-up interne activée.
    // État repos = HIGH, appui = LOW (front descendant).
    // Pas de résistance externe nécessaire entre D2 et GND.
    pinMode(FP3_BUTTON_PIN, INPUT_PULLUP);
    buttonLastState    = HIGH;
    buttonLastChangeMs = millis();
    Serial.print("FP3 button pin : D"); Serial.println(FP3_BUTTON_PIN);
    Serial.print("FP3 capture    : "); Serial.print(FP3_CAPTURE_SAMPLES);
    Serial.println(" samples @ 8 kHz (3 s)");
    Serial.println("FP3 pret — appuyez D2 pour enregistrer.");
#endif

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

#if !defined(FP1_PURE) || (FP1_PURE == 0)
    // --- FP3 : vérification bouton (non-bloquant, ~3 µs) ---
    fp3_check_button();
#endif

    // --- Consommation du buffer ADC et filtrage ---

    uint32_t head = bufHead;   // lecture atomique (32 bits ARM)
    uint32_t tail = bufTail;

    while (tail != head)
    {
        uint16_t rawSample = adcBuffer[tail];
        tail = (tail + 1) & BUFFER_MASK;

#if defined(FP1_PURE) && (FP1_PURE == 1)
        // ====================================================================
        // Mode FP1 PURE : pas de filtrage, DAC0 restitue l'ADC brut.
        // Objectif : validation ET1 oscilloscope (Te = 31.25 µs sur marches
        // DAC) et démo repliement spectral à 17 kHz (alias à 15 kHz visible).
        // ====================================================================
        int16_t filtered = (int16_t)((int32_t)rawSample - (int32_t)ADC_MIDSCALE);
        analogWrite(DAC_RAW_PIN, (uint32_t)rawSample);
#else
        // ====================================================================
        // Mode FP1+FP2 : double sortie DAC pour démo visuelle ET2 à l'oscillo.
        //   - DAC0 = rawSample     (référence : signal ADC brut, avant filtre)
        //   - DAC1 = filtered + DC (signal filtré : doit s'effondrer > 4 kHz)
        // Injecter un signal à 4 kHz : CH1 (DAC0) reste intact, CH2 (DAC1)
        // chute de -41 dB → rapport visuel x110 sur l'amplitude.
        // ====================================================================

        // --- DAC0 = signal brut (reference), sorti AVANT le filtre ---
        analogWrite(DAC_RAW_PIN, (uint32_t)rawSample);

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

        // --- DAC1 = signal filtre (validation visuelle ET2) ---
        // filtered est centré sur 0, plage [-2048, +2047].
        // DAC attend [0, 4095] → on rajoute l'offset DC 2048.
        // Clamp int32 avant cast pour éviter tout débordement sur signal saturé.
        int32_t dacVal = (int32_t)filtered + (int32_t)ADC_MIDSCALE;
        if (dacVal < 0)    { dacVal = 0;    }
        if (dacVal > 4095) { dacVal = 4095; }
        analogWrite(DAC_FILTERED_PIN, (uint32_t)dacVal);
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

#if !defined(FP1_PURE) || (FP1_PURE == 0)
            // --- FP3 ARMING : copie dans captureBuffer ---
            // fp3_push_sample() est un simple store + incrément quand
            // fp3State == FP3_IDLE, donc son coût est quasi nul (1 test).
            fp3_push_sample((int16_t)s16val);
#endif
        }
    }

    // Mettre à jour l'index de lecture (une seule écriture volatile à la fin)
    bufTail = tail;

#if !defined(FP1_PURE) || (FP1_PURE == 0)
    // --- FP3 DUMPING : service non-bloquant du transfert série ---
    // Appelé APRÈS la consommation du buffer ADC pour ne pas retarder
    // le filtrage. fp3_service_dump() envoie autant d'octets que le
    // buffer TX série peut en absorber sans bloquer, puis rend la main.
    fp3_service_dump();
#endif

    // --- Mesure de fréquence réelle + log FP1/FP2 (~1 fois/seconde) ---
    //
    // IMPORTANT : on SUSPEND les logs pendant DUMPING, sinon les caractères
    // ASCII s'intercalent dans le flux binaire FP3 et corrompent le fichier
    // .wav reconstitué côté PC (le parser Python lit N octets consécutifs
    // en pensant recevoir du PCM, mais certains sont en réalité les lettres
    // de "[FP1] Fe_reelle=..." → alignement perdu, footer magic décalé).
    //
    // On met aussi à jour lastFreqMeasureUs pendant la suspension pour
    // éviter un log "rattrapage" monstre juste après la fin du dump.

    uint32_t nowUs = micros();
#if !defined(FP1_PURE) || (FP1_PURE == 0)
    if (fp3State == FP3_DUMPING)
    {
        // Pendant le dump binaire : on décale lastFreqMeasureUs pour que
        // le log ne "rattrape" pas d'un coup quand on revient en IDLE.
        lastFreqMeasureUs = nowUs;
        // Remise à zéro du compteur pour que le prochain log affiche une
        // mesure sur une seule seconde (pas cumulée depuis le dump).
        NVIC_DisableIRQ(TC0_IRQn);
        sampleCount = 0;
        NVIC_EnableIRQ(TC0_IRQn);
    }
    else
#endif
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
            Serial.print(FILTER_TAPS);
#if !defined(FP1_PURE) || (FP1_PURE == 0)
            Serial.print(" | fp3=");
            Serial.print(fp3_state_str());
            if (fp3State == FP3_ARMING)
            {
                // Afficher la progression de la capture (utile pour debug)
                Serial.print(" (");
                Serial.print(captureIdx);
                Serial.print("/");
                Serial.print(FP3_CAPTURE_SAMPLES);
                Serial.print(")");
            }
#endif
            Serial.println();

            // Reset des accumulateurs de timing
            timingCount   = 0;
            timingAccumUs = 0;
            timingMaxUs   = 0;
        }

        lastFreqMeasureUs = nowUs;
    }
}
