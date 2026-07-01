#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "GameFramework/Actor.h"

#include "AssistantTypes.h"

#include "PersonDetectionActor.generated.h"

class UAssistantSubsystem;
struct FPersonDetectionOpenCVState;

/**
 * Risultato prodotto dal task OpenCV in background.
 * Non e' un USTRUCT perche' viene usato soltanto internamente in C++.
 */
struct FPersonDetectionAsyncResult
{
    bool bFrameReadSucceeded = false;
    bool bPersonDetected = false;
    int32 DetectionCount = 0;
    FString ErrorMessage;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FPersonRawPresenceChangedSignature,
    bool,
    bPersonDetected
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FPersonPresenceConfirmedSignature
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FPersonPresenceRearmedSignature
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(
    FAutomaticInteractionStartedSignature
);

/**
 * Acquisisce una webcam tramite OpenCV e rileva la presenza di una persona.
 *
 * Strategia:
 * 1. rilevamento volto tramite Haar Cascade;
 * 2. fallback opzionale HOG per persona a figura intera;
 * 3. conferma soltanto dopo una presenza stabile;
 * 4. avvio automatico dell'AssistantSubsystem;
 * 5. dopo ogni interazione richiede un'assenza reale e stabile prima
 *    di consentire un nuovo avvio.
 */
UCLASS(Blueprintable)
class AVATARZG_API APersonDetectionActor : public AActor
{
    GENERATED_BODY()

public:
    APersonDetectionActor();
    virtual ~APersonDetectionActor() override;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(
        const EEndPlayReason::Type EndPlayReason
    ) override;

public:
    // ---------------------------------------------------------------------
    // Eventi Blueprint
    // ---------------------------------------------------------------------

    UPROPERTY(
        BlueprintAssignable,
        Category = "Person Detection|Events"
    )
    FPersonRawPresenceChangedSignature OnRawPresenceChanged;

    UPROPERTY(
        BlueprintAssignable,
        Category = "Person Detection|Events"
    )
    FPersonPresenceConfirmedSignature OnPresenceConfirmed;

    UPROPERTY(
        BlueprintAssignable,
        Category = "Person Detection|Events"
    )
    FPersonPresenceRearmedSignature OnPresenceRearmed;

    UPROPERTY(
        BlueprintAssignable,
        Category = "Person Detection|Events"
    )
    FAutomaticInteractionStartedSignature OnAutomaticInteractionStarted;

    // ---------------------------------------------------------------------
    // Avvio e integrazione con AssistantSubsystem
    // ---------------------------------------------------------------------

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Startup"
    )
    bool bStartDetectionOnBeginPlay = true;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Startup"
    )
    bool bAutoStartInteraction = true;

    /**
     * Se valorizzato, viene applicato al subsystem prima di StartInteraction.
     * Lasciare vuoto se il backend viene gia' configurato altrove a BeginPlay.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Assistant"
    )
    FString BackendEndpointUrl =
        TEXT("http://127.0.0.1:8010/assistant/query");

    /**
     * Se valorizzato, viene applicato al subsystem prima di StartInteraction.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Assistant"
    )
    FString MallId = TEXT("GranShoppingMolfetta");

    // ---------------------------------------------------------------------
    // Webcam
    // ---------------------------------------------------------------------

    /**
     * Indice OpenCV della webcam. Normalmente 0 e' la prima webcam.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Camera",
        meta = (ClampMin = "0")
    )
    int32 CameraDeviceIndex = 0;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Camera",
        meta = (ClampMin = "160")
    )
    int32 RequestedCaptureWidth = 640;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Camera",
        meta = (ClampMin = "120")
    )
    int32 RequestedCaptureHeight = 480;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Camera",
        meta = (ClampMin = "1")
    )
    int32 RequestedCaptureFps = 30;

    /**
     * Frequenza con cui viene avviata una nuova analisi.
     * 0.20 equivale a circa 5 analisi al secondo.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Camera",
        meta = (ClampMin = "0.05")
    )
    float DetectionIntervalSeconds = 0.20f;

    /**
     * Il frame viene ridimensionato a questa larghezza prima dell'analisi.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Camera",
        meta = (ClampMin = "160")
    )
    int32 DetectionFrameWidth = 320;

    // ---------------------------------------------------------------------
    // Stabilizzazione della presenza
    // ---------------------------------------------------------------------

    /**
     * Tempo di presenza stabile richiesto prima dell'avvio automatico.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Timing",
        meta = (ClampMin = "0.1")
    )
    float RequiredPresenceDurationSeconds = 5.0f;

    /**
     * Dopo una conversazione, la persona deve risultare assente per questo
     * intervallo prima che il detector possa riarmarsi.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Timing",
        meta = (ClampMin = "0.1")
    )
    float RequiredAbsenceDurationSeconds = 3.0f;

    /**
     * Piccole mancate rilevazioni inferiori a questo valore non azzerano
     * immediatamente il conteggio della presenza.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Timing",
        meta = (ClampMin = "0.0")
    )
    float PresenceDropoutGraceSeconds = 1.0f;

    // ---------------------------------------------------------------------
    // Haar face detector
    // ---------------------------------------------------------------------

    /**
     * Percorso relativo a Content oppure percorso assoluto.
     *
     * Valore previsto:
     * Content/PersonDetection/haarcascade_frontalface_default.xml
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Face Detector"
    )
    FString FaceCascadePath =
        TEXT("PersonDetection/haarcascade_frontalface_default.xml");

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Face Detector",
        meta = (ClampMin = "1.01", ClampMax = "2.0")
    )
    float FaceScaleFactor = 1.10f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Face Detector",
        meta = (ClampMin = "1")
    )
    int32 FaceMinNeighbors = 4;

    /**
     * Dimensione minima del volto nel frame ridimensionato.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Face Detector",
        meta = (ClampMin = "16")
    )
    int32 MinimumFaceSizePixels = 48;

    // ---------------------------------------------------------------------
    // HOG person detector
    // ---------------------------------------------------------------------

    /**
     * Fallback integrato che non richiede file esterni.
     * E' piu' adatto a persone visibili quasi a figura intera.
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|HOG"
    )
    bool bEnableHogFallback = true;

    // ---------------------------------------------------------------------
    // Debug e stato runtime
    // ---------------------------------------------------------------------

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Person Detection|Debug"
    )
    bool bPrintDebug = true;

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Person Detection|Runtime"
    )
    bool bCameraRunning = false;

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Person Detection|Runtime"
    )
    bool bRawPersonDetected = false;

    /**
     * Indica che una presenza e' stata confermata per il ciclo corrente.
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Person Detection|Runtime"
    )
    bool bPresenceConfirmed = false;

    /**
     * Quando e' true, nessuna nuova interazione puo' partire finche' non viene
     * osservata un'assenza stabile mentre l'assistente e' nuovamente in Idle.
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Person Detection|Runtime"
    )
    bool bWaitingForFreshAbsence = false;

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Person Detection|Runtime"
    )
    float PresenceProgress01 = 0.0f;

    UPROPERTY(
        BlueprintReadOnly,
        Category = "Person Detection|Runtime"
    )
    int32 LastDetectionCount = 0;

    // ---------------------------------------------------------------------
    // API
    // ---------------------------------------------------------------------

    UFUNCTION(
        BlueprintCallable,
        Category = "Person Detection"
    )
    bool StartDetection();

    UFUNCTION(
        BlueprintCallable,
        Category = "Person Detection"
    )
    void StopDetection();

    /**
     * Reset manuale disponibile per il debug.
     * Durante una conversazione attiva il reset viene ignorato.
     */
    UFUNCTION(
        BlueprintCallable,
        Category = "Person Detection"
    )
    void ResetPresenceLatch();

private:
    UPROPERTY()
    TObjectPtr<UAssistantSubsystem> AssistantSubsystem = nullptr;

    TSharedPtr<
        FPersonDetectionOpenCVState,
        ESPMode::ThreadSafe
    > OpenCVState;

    TFuture<FPersonDetectionAsyncResult> PendingDetectionFuture;
    bool bHasPendingDetectionTask = false;

    FTimerHandle DetectionTimerHandle;

    float ContinuousPresenceSeconds = 0.0f;
    float ContinuousAbsenceSeconds = 0.0f;

    double LastDetectionResultTimeSeconds = 0.0;

    int32 ConsecutiveFrameReadFailures = 0;

    UFUNCTION()
    void HandleAssistantStateChanged(
        EAssistantState NewState,
        EAssistantState PreviousState
    );

    void ProcessDetectionTimer();
    void StartNextDetectionTask();

    void ApplyDetectionResult(
        const FPersonDetectionAsyncResult& Result
    );

    void ConfirmPresenceAndMaybeStartInteraction();
    void LockUntilFreshAbsence(const FString& Reason);
    void RearmAfterAbsence();

    bool IsAssistantIdleAndInactive() const;
    bool CanStartInteraction() const;
    FString ResolveFaceCascadePath() const;

    void SetRawPresence(bool bDetected);
    void ResetRuntimeTracking(bool bResetLatch);

    void DebugLog(const FString& Message) const;
};
