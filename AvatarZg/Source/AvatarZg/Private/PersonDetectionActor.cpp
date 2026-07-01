#include "PersonDetectionActor.h"

#include "AssistantSubsystem.h"

#include "Async/Async.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

#include "PreOpenCVHeaders.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/videoio.hpp>
#include "PostOpenCVHeaders.h"

DEFINE_LOG_CATEGORY_STATIC(
    LogPersonDetectionActor,
    Log,
    All
);

struct FPersonDetectionOpenCVState
{
    cv::VideoCapture Camera;
    cv::CascadeClassifier FaceCascade;
    cv::HOGDescriptor HogDescriptor;

    bool bFaceCascadeLoaded = false;
    bool bHogReady = false;
};

APersonDetectionActor::APersonDetectionActor()
{
    PrimaryActorTick.bCanEverTick = false;
}

APersonDetectionActor::~APersonDetectionActor() = default;

void APersonDetectionActor::BeginPlay()
{
    Super::BeginPlay();

    if (UGameInstance* GameInstance = GetGameInstance())
    {
        AssistantSubsystem =
            GameInstance->GetSubsystem<UAssistantSubsystem>();
    }

    if (AssistantSubsystem)
    {
        AssistantSubsystem->OnStateChanged.AddDynamic(
            this,
            &APersonDetectionActor::HandleAssistantStateChanged
        );

        if (!IsAssistantIdleAndInactive())
        {
            LockUntilFreshAbsence(
                TEXT("Assistant gia' attivo a BeginPlay")
            );
        }
    }
    else
    {
        DebugLog(
            TEXT("BeginPlay: AssistantSubsystem non trovato.")
        );
    }

    if (bStartDetectionOnBeginPlay)
    {
        StartDetection();
    }
}

void APersonDetectionActor::EndPlay(
    const EEndPlayReason::Type EndPlayReason
)
{
    if (AssistantSubsystem)
    {
        AssistantSubsystem->OnStateChanged.RemoveDynamic(
            this,
            &APersonDetectionActor::HandleAssistantStateChanged
        );
    }

    StopDetection();

    AssistantSubsystem = nullptr;

    Super::EndPlay(EndPlayReason);
}

void APersonDetectionActor::HandleAssistantStateChanged(
    EAssistantState NewState,
    EAssistantState PreviousState
)
{
    if (NewState != EAssistantState::Idle)
    {
        LockUntilFreshAbsence(
            FString::Printf(
                TEXT("Assistant entrato nello stato %d"),
                static_cast<int32>(NewState)
            )
        );

        return;
    }

    /*
     * Quando la conversazione termina non riarmiamo subito il detector.
     * Da questo momento deve essere osservata un'assenza reale e stabile.
     */
    ContinuousPresenceSeconds = 0.0f;
    ContinuousAbsenceSeconds = 0.0f;
    PresenceProgress01 = 0.0f;
    LastDetectionResultTimeSeconds = FPlatformTime::Seconds();

    if (bWaitingForFreshAbsence)
    {
        DebugLog(
            TEXT(
                "Assistant tornato in Idle: attendo un'assenza "
                "stabile prima del riarmo."
            )
        );
    }
}

bool APersonDetectionActor::StartDetection()
{
    StopDetection();

    ResetRuntimeTracking(true);

    DetectionIntervalSeconds =
        FMath::Max(0.05f, DetectionIntervalSeconds);

    RequiredPresenceDurationSeconds =
        FMath::Max(0.1f, RequiredPresenceDurationSeconds);

    RequiredAbsenceDurationSeconds =
        FMath::Max(0.1f, RequiredAbsenceDurationSeconds);

    PresenceDropoutGraceSeconds =
        FMath::Max(0.0f, PresenceDropoutGraceSeconds);

    if (AssistantSubsystem && !IsAssistantIdleAndInactive())
    {
        LockUntilFreshAbsence(
            TEXT("Detection avviato mentre l'Assistant non e' Idle")
        );
    }

    TSharedPtr<
        FPersonDetectionOpenCVState,
        ESPMode::ThreadSafe
    > NewState =
        MakeShared<
        FPersonDetectionOpenCVState,
        ESPMode::ThreadSafe
        >();

    try
    {
        const FString CascadePath =
            ResolveFaceCascadePath();

        if (
            !CascadePath.IsEmpty()
            && IFileManager::Get().FileExists(*CascadePath)
            )
        {
            const FTCHARToUTF8 CascadePathUtf8(*CascadePath);

            NewState->bFaceCascadeLoaded =
                NewState->FaceCascade.load(
                    CascadePathUtf8.Get()
                );

            if (NewState->bFaceCascadeLoaded)
            {
                DebugLog(
                    FString::Printf(
                        TEXT("Face cascade caricata: %s"),
                        *CascadePath
                    )
                );
            }
            else
            {
                DebugLog(
                    FString::Printf(
                        TEXT(
                            "Face cascade presente ma non caricabile: %s"
                        ),
                        *CascadePath
                    )
                );
            }
        }
        else
        {
            DebugLog(
                FString::Printf(
                    TEXT(
                        "Face cascade non trovata: %s. "
                        "Rimarra' disponibile il fallback HOG."
                    ),
                    *CascadePath
                )
            );
        }

        if (bEnableHogFallback)
        {
            NewState->HogDescriptor.setSVMDetector(
                cv::HOGDescriptor::getDefaultPeopleDetector()
            );

            NewState->bHogReady = true;
        }

        bool bOpened = false;

#if PLATFORM_WINDOWS
        bOpened = NewState->Camera.open(
            CameraDeviceIndex,
            cv::CAP_DSHOW
        );

        if (!bOpened)
        {
            NewState->Camera.release();

            bOpened = NewState->Camera.open(
                CameraDeviceIndex,
                cv::CAP_MSMF
            );
        }
#endif

        if (!bOpened)
        {
            NewState->Camera.release();

            bOpened = NewState->Camera.open(
                CameraDeviceIndex,
                cv::CAP_ANY
            );
        }

        if (!bOpened || !NewState->Camera.isOpened())
        {
            DebugLog(
                FString::Printf(
                    TEXT(
                        "Impossibile aprire la webcam con indice %d."
                    ),
                    CameraDeviceIndex
                )
            );

            return false;
        }

        NewState->Camera.set(
            cv::CAP_PROP_FRAME_WIDTH,
            FMath::Max(160, RequestedCaptureWidth)
        );

        NewState->Camera.set(
            cv::CAP_PROP_FRAME_HEIGHT,
            FMath::Max(120, RequestedCaptureHeight)
        );

        NewState->Camera.set(
            cv::CAP_PROP_FPS,
            FMath::Max(1, RequestedCaptureFps)
        );
    }
    catch (const cv::Exception& Error)
    {
        DebugLog(
            FString::Printf(
                TEXT("Errore OpenCV durante StartDetection: %s"),
                UTF8_TO_TCHAR(Error.what())
            )
        );

        return false;
    }

    OpenCVState = NewState;
    bCameraRunning = true;

    GetWorldTimerManager().SetTimer(
        DetectionTimerHandle,
        this,
        &APersonDetectionActor::ProcessDetectionTimer,
        DetectionIntervalSeconds,
        true,
        0.0f
    );

    DebugLog(
        FString::Printf(
            TEXT(
                "Webcam avviata. Device=%d, intervallo=%.2f s, "
                "presenza richiesta=%.1f s, assenza richiesta=%.1f s."
            ),
            CameraDeviceIndex,
            DetectionIntervalSeconds,
            RequiredPresenceDurationSeconds,
            RequiredAbsenceDurationSeconds
        )
    );

    return true;
}

void APersonDetectionActor::StopDetection()
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(
            DetectionTimerHandle
        );
    }

    /*
     * Il task usa una copia thread-safe di OpenCVState.
     * Attendere qui evita di rilasciare la webcam mentre OpenCV
     * sta ancora leggendo o analizzando il frame.
     */
    if (bHasPendingDetectionTask)
    {
        PendingDetectionFuture.Wait();
        PendingDetectionFuture.Get();

        bHasPendingDetectionTask = false;
    }

    if (OpenCVState.IsValid())
    {
        try
        {
            if (OpenCVState->Camera.isOpened())
            {
                OpenCVState->Camera.release();
            }
        }
        catch (const cv::Exception& Error)
        {
            DebugLog(
                FString::Printf(
                    TEXT(
                        "Errore OpenCV durante StopDetection: %s"
                    ),
                    UTF8_TO_TCHAR(Error.what())
                )
            );
        }
    }

    OpenCVState.Reset();

    bCameraRunning = false;

    ResetRuntimeTracking(true);
    bWaitingForFreshAbsence = false;
}

void APersonDetectionActor::ResetPresenceLatch()
{
    if (AssistantSubsystem && !IsAssistantIdleAndInactive())
    {
        DebugLog(
            TEXT(
                "ResetPresenceLatch ignorato: "
                "l'Assistant non e' in Idle."
            )
        );

        return;
    }

    const bool bWasLocked =
        bPresenceConfirmed || bWaitingForFreshAbsence;

    bPresenceConfirmed = false;
    bWaitingForFreshAbsence = false;

    ContinuousPresenceSeconds = 0.0f;
    ContinuousAbsenceSeconds = 0.0f;
    PresenceProgress01 = 0.0f;

    if (bWasLocked)
    {
        OnPresenceRearmed.Broadcast();
        DebugLog(TEXT("Presence latch resettato manualmente."));
    }
}

void APersonDetectionActor::ProcessDetectionTimer()
{
    if (!bCameraRunning || !OpenCVState.IsValid())
    {
        return;
    }

    if (bHasPendingDetectionTask)
    {
        if (!PendingDetectionFuture.IsReady())
        {
            return;
        }

        const FPersonDetectionAsyncResult Result =
            PendingDetectionFuture.Get();

        bHasPendingDetectionTask = false;

        ApplyDetectionResult(Result);
    }

    if (
        bCameraRunning
        && OpenCVState.IsValid()
        && !bHasPendingDetectionTask
        )
    {
        StartNextDetectionTask();
    }
}

void APersonDetectionActor::StartNextDetectionTask()
{
    if (!OpenCVState.IsValid())
    {
        return;
    }

    const TSharedPtr<
        FPersonDetectionOpenCVState,
        ESPMode::ThreadSafe
    > State = OpenCVState;

    const int32 LocalDetectionFrameWidth =
        FMath::Max(160, DetectionFrameWidth);

    const float LocalFaceScaleFactor =
        FMath::Clamp(FaceScaleFactor, 1.01f, 2.0f);

    const int32 LocalFaceMinNeighbors =
        FMath::Max(1, FaceMinNeighbors);

    const int32 LocalMinimumFaceSize =
        FMath::Max(16, MinimumFaceSizePixels);

    const bool bLocalEnableHogFallback =
        bEnableHogFallback;

    PendingDetectionFuture =
        Async(
            EAsyncExecution::ThreadPool,
            [
                State,
                LocalDetectionFrameWidth,
                LocalFaceScaleFactor,
                LocalFaceMinNeighbors,
                LocalMinimumFaceSize,
                bLocalEnableHogFallback
            ]() -> FPersonDetectionAsyncResult
            {
                FPersonDetectionAsyncResult Result;

                if (!State.IsValid())
                {
                    Result.ErrorMessage =
                        TEXT("Stato OpenCV non valido.");

                    return Result;
                }

                try
                {
                    cv::Mat CapturedFrame;

                    if (
                        !State->Camera.isOpened()
                        || !State->Camera.read(CapturedFrame)
                        || CapturedFrame.empty()
                        )
                    {
                        Result.ErrorMessage =
                            TEXT("Lettura del frame webcam fallita.");

                        return Result;
                    }

                    Result.bFrameReadSucceeded = true;

                    cv::Mat DetectionFrame;

                    if (
                        CapturedFrame.cols
                        > LocalDetectionFrameWidth
                        )
                    {
                        const double Scale =
                            static_cast<double>(
                                LocalDetectionFrameWidth
                                )
                            / static_cast<double>(
                                CapturedFrame.cols
                                );

                        const int32 TargetHeight =
                            FMath::Max(
                                1,
                                FMath::RoundToInt(
                                    CapturedFrame.rows * Scale
                                )
                            );

                        cv::resize(
                            CapturedFrame,
                            DetectionFrame,
                            cv::Size(
                                LocalDetectionFrameWidth,
                                TargetHeight
                            ),
                            0.0,
                            0.0,
                            cv::INTER_AREA
                        );
                    }
                    else
                    {
                        DetectionFrame = CapturedFrame;
                    }

                    std::vector<cv::Rect> FaceRectangles;

                    if (State->bFaceCascadeLoaded)
                    {
                        cv::Mat GrayFrame;

                        if (DetectionFrame.channels() == 4)
                        {
                            cv::cvtColor(
                                DetectionFrame,
                                GrayFrame,
                                cv::COLOR_BGRA2GRAY
                            );
                        }
                        else if (DetectionFrame.channels() == 3)
                        {
                            cv::cvtColor(
                                DetectionFrame,
                                GrayFrame,
                                cv::COLOR_BGR2GRAY
                            );
                        }
                        else if (DetectionFrame.channels() == 1)
                        {
                            GrayFrame = DetectionFrame;
                        }
                        else
                        {
                            Result.ErrorMessage =
                                TEXT(
                                    "Formato colore webcam non supportato."
                                );

                            return Result;
                        }

                        cv::equalizeHist(
                            GrayFrame,
                            GrayFrame
                        );

                        State->FaceCascade.detectMultiScale(
                            GrayFrame,
                            FaceRectangles,
                            static_cast<double>(
                                LocalFaceScaleFactor
                                ),
                            LocalFaceMinNeighbors,
                            cv::CASCADE_SCALE_IMAGE,
                            cv::Size(
                                LocalMinimumFaceSize,
                                LocalMinimumFaceSize
                            )
                        );
                    }

                    if (!FaceRectangles.empty())
                    {
                        Result.bPersonDetected = true;
                        Result.DetectionCount =
                            static_cast<int32>(
                                FaceRectangles.size()
                                );

                        return Result;
                    }

                    if (
                        bLocalEnableHogFallback
                        && State->bHogReady
                        )
                    {
                        std::vector<cv::Rect> PersonRectangles;
                        std::vector<double> DetectionWeights;

                        State->HogDescriptor.detectMultiScale(
                            DetectionFrame,
                            PersonRectangles,
                            DetectionWeights,
                            0.0,
                            cv::Size(8, 8),
                            cv::Size(8, 8),
                            1.05,
                            2.0,
                            false
                        );

                        Result.DetectionCount =
                            static_cast<int32>(
                                PersonRectangles.size()
                                );

                        Result.bPersonDetected =
                            !PersonRectangles.empty();
                    }
                }
                catch (const cv::Exception& Error)
                {
                    Result.bFrameReadSucceeded = false;
                    Result.bPersonDetected = false;
                    Result.DetectionCount = 0;
                    Result.ErrorMessage =
                        UTF8_TO_TCHAR(Error.what());
                }

                return Result;
            }
        );

    bHasPendingDetectionTask = true;
}

void APersonDetectionActor::ApplyDetectionResult(
    const FPersonDetectionAsyncResult& Result
)
{
    const double NowSeconds =
        FPlatformTime::Seconds();

    float ResultDeltaSeconds =
        DetectionIntervalSeconds;

    if (LastDetectionResultTimeSeconds > 0.0)
    {
        ResultDeltaSeconds =
            static_cast<float>(
                NowSeconds - LastDetectionResultTimeSeconds
                );

        ResultDeltaSeconds =
            FMath::Clamp(
                ResultDeltaSeconds,
                0.0f,
                1.0f
            );
    }

    LastDetectionResultTimeSeconds = NowSeconds;

    if (!Result.bFrameReadSucceeded)
    {
        ++ConsecutiveFrameReadFailures;

        if (
            ConsecutiveFrameReadFailures == 1
            || ConsecutiveFrameReadFailures % 20 == 0
            )
        {
            DebugLog(
                FString::Printf(
                    TEXT(
                        "Frame webcam non disponibile. "
                        "Errori consecutivi=%d. Dettaglio=%s"
                    ),
                    ConsecutiveFrameReadFailures,
                    *Result.ErrorMessage
                )
            );
        }

        SetRawPresence(false);
        LastDetectionCount = 0;

        /*
         * Un errore di acquisizione non viene considerato un'assenza valida:
         * in caso contrario una webcam instabile potrebbe riarmare il sistema.
         */
        ContinuousPresenceSeconds = 0.0f;
        PresenceProgress01 = 0.0f;

        return;
    }

    ConsecutiveFrameReadFailures = 0;
    LastDetectionCount = Result.DetectionCount;

    SetRawPresence(Result.bPersonDetected);

    /*
     * Durante Greeting, Listening, QueryingAI, Speaking,
     * WaitingForFollowUp ed EndingInteraction non accumuliamo ne' presenza
     * ne' assenza per un nuovo ciclo.
     */
    if (AssistantSubsystem && !IsAssistantIdleAndInactive())
    {
        LockUntilFreshAbsence(
            TEXT("Assistant non Idle durante l'analisi webcam")
        );

        return;
    }

    /*
     * Dopo una conversazione il detector resta bloccato finche', mentre
     * l'Assistant e' in Idle, non viene osservata un'assenza stabile.
     */
    if (bWaitingForFreshAbsence)
    {
        ContinuousPresenceSeconds = 0.0f;
        PresenceProgress01 = 0.0f;

        if (Result.bPersonDetected)
        {
            ContinuousAbsenceSeconds = 0.0f;
        }
        else
        {
            ContinuousAbsenceSeconds +=
                ResultDeltaSeconds;
        }

        if (
            ContinuousAbsenceSeconds
            >= FMath::Max(
                0.1f,
                RequiredAbsenceDurationSeconds
            )
            )
        {
            RearmAfterAbsence();
        }

        return;
    }

    if (Result.bPersonDetected)
    {
        ContinuousPresenceSeconds +=
            ResultDeltaSeconds;

        ContinuousAbsenceSeconds = 0.0f;
    }
    else
    {
        ContinuousAbsenceSeconds +=
            ResultDeltaSeconds;

        if (
            ContinuousAbsenceSeconds
            > PresenceDropoutGraceSeconds
            )
        {
            ContinuousPresenceSeconds = 0.0f;
        }
    }

    const float SafePresenceDuration =
        FMath::Max(
            0.1f,
            RequiredPresenceDurationSeconds
        );

    PresenceProgress01 =
        FMath::Clamp(
            ContinuousPresenceSeconds
            / SafePresenceDuration,
            0.0f,
            1.0f
        );

    if (
        !bPresenceConfirmed
        && ContinuousPresenceSeconds
        >= SafePresenceDuration
        )
    {
        if (!bAutoStartInteraction || CanStartInteraction())
        {
            ConfirmPresenceAndMaybeStartInteraction();
        }
    }
}

void APersonDetectionActor::ConfirmPresenceAndMaybeStartInteraction()
{
    if (bPresenceConfirmed || bWaitingForFreshAbsence)
    {
        return;
    }

    bPresenceConfirmed = true;
    bWaitingForFreshAbsence = true;
    PresenceProgress01 = 1.0f;

    DebugLog(
        FString::Printf(
            TEXT(
                "PRESENZA CONFERMATA dopo %.2f secondi."
            ),
            ContinuousPresenceSeconds
        )
    );

    OnPresenceConfirmed.Broadcast();

    if (!bAutoStartInteraction)
    {
        return;
    }

    if (!AssistantSubsystem)
    {
        if (UGameInstance* GameInstance = GetGameInstance())
        {
            AssistantSubsystem =
                GameInstance->GetSubsystem<UAssistantSubsystem>();
        }
    }

    if (!AssistantSubsystem || !CanStartInteraction())
    {
        DebugLog(
            TEXT(
                "Presenza confermata, ma l'Assistant "
                "non e' disponibile o non e' in Idle."
            )
        );

        return;
    }

    const FString CleanEndpoint =
        BackendEndpointUrl.TrimStartAndEnd();

    if (!CleanEndpoint.IsEmpty())
    {
        AssistantSubsystem->SetBackendEndpoint(
            CleanEndpoint
        );
    }

    const FString CleanMallId =
        MallId.TrimStartAndEnd();

    if (!CleanMallId.IsEmpty())
    {
        AssistantSubsystem->SetMallId(
            CleanMallId
        );
    }

    AssistantSubsystem->StartInteraction();

    OnAutomaticInteractionStarted.Broadcast();

    DebugLog(
        TEXT(
            "Interazione avviata automaticamente dalla webcam."
        )
    );
}

void APersonDetectionActor::LockUntilFreshAbsence(
    const FString& Reason
)
{
    const bool bWasAlreadyLocked =
        bWaitingForFreshAbsence;

    bWaitingForFreshAbsence = true;

    ContinuousPresenceSeconds = 0.0f;
    ContinuousAbsenceSeconds = 0.0f;
    PresenceProgress01 = 0.0f;

    if (!bWasAlreadyLocked)
    {
        DebugLog(
            FString::Printf(
                TEXT(
                    "Detector bloccato fino a una nuova assenza stabile. "
                    "Motivo: %s"
                ),
                *Reason
            )
        );
    }
}

void APersonDetectionActor::RearmAfterAbsence()
{
    bPresenceConfirmed = false;
    bWaitingForFreshAbsence = false;

    ContinuousPresenceSeconds = 0.0f;
    ContinuousAbsenceSeconds = 0.0f;
    PresenceProgress01 = 0.0f;

    OnPresenceRearmed.Broadcast();

    DebugLog(
        TEXT(
            "Rilevamento riarmato dopo un'assenza stabile."
        )
    );
}

bool APersonDetectionActor::IsAssistantIdleAndInactive() const
{
    if (!AssistantSubsystem)
    {
        return true;
    }

    return !AssistantSubsystem->IsInteractionActive()
        && AssistantSubsystem->GetCurrentState()
        == EAssistantState::Idle;
}

bool APersonDetectionActor::CanStartInteraction() const
{
    return AssistantSubsystem
        && IsAssistantIdleAndInactive();
}

FString APersonDetectionActor::ResolveFaceCascadePath() const
{
    const FString CleanPath =
        FaceCascadePath.TrimStartAndEnd();

    if (CleanPath.IsEmpty())
    {
        return FString();
    }

    FString ResolvedPath;

    if (FPaths::IsRelative(CleanPath))
    {
        ResolvedPath = FPaths::Combine(
            FPaths::ProjectContentDir(),
            CleanPath
        );
    }
    else
    {
        ResolvedPath = CleanPath;
    }

    ResolvedPath =
        FPaths::ConvertRelativePathToFull(
            ResolvedPath
        );

    FPaths::NormalizeFilename(ResolvedPath);

    return ResolvedPath;
}

void APersonDetectionActor::SetRawPresence(bool bDetected)
{
    if (bRawPersonDetected == bDetected)
    {
        return;
    }

    bRawPersonDetected = bDetected;

    OnRawPresenceChanged.Broadcast(
        bRawPersonDetected
    );

    DebugLog(
        bRawPersonDetected
        ? TEXT("RAW PERSON DETECTED")
        : TEXT("RAW PERSON LOST")
    );
}

void APersonDetectionActor::ResetRuntimeTracking(
    bool bResetLatch
)
{
    SetRawPresence(false);

    ContinuousPresenceSeconds = 0.0f;
    ContinuousAbsenceSeconds = 0.0f;

    PresenceProgress01 = 0.0f;
    LastDetectionCount = 0;
    LastDetectionResultTimeSeconds = 0.0;

    ConsecutiveFrameReadFailures = 0;

    if (bResetLatch)
    {
        bPresenceConfirmed = false;
        bWaitingForFreshAbsence = false;
    }
}

void APersonDetectionActor::DebugLog(
    const FString& Message
) const
{
    if (!bPrintDebug)
    {
        return;
    }

    UE_LOG(
        LogPersonDetectionActor,
        Warning,
        TEXT("%s"),
        *Message
    );
}
