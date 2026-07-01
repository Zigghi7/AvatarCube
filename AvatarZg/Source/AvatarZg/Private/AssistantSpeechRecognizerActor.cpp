#include "AssistantSpeechRecognizerActor.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

#include "Sound/CapturableSoundWave.h"
#include "SpeechRecognizer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssistantSpeechRecognizerActor, Log, All);

AAssistantSpeechRecognizerActor::AAssistantSpeechRecognizerActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAssistantSpeechRecognizerActor::BeginPlay()
{
	Super::BeginPlay();

	bShuttingDown = false;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		AssistantSubsystem =
			GameInstance->GetSubsystem<UAssistantSubsystem>();
	}

	if (!AssistantSubsystem)
	{
		DebugLog(TEXT("BeginPlay: AssistantSubsystem non trovato."));
		return;
	}

	AssistantSubsystem->OnStateChanged.AddDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleAssistantStateChanged
	);

	InitializeSpeechRecognizer();
}

void AAssistantSpeechRecognizerActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason
)
{
	bShuttingDown = true;
	bPendingAutomaticListen = false;
	bDiscardRecognitionResult = true;

	GetWorldTimerManager().ClearTimer(RetryRecognitionTimerHandle);
	ClearMaximumUtteranceTimer();

	if (AssistantSubsystem)
	{
		AssistantSubsystem->OnStateChanged.RemoveDynamic(
			this,
			&AAssistantSpeechRecognizerActor::HandleAssistantStateChanged
		);
	}

	StopCaptureIfNeeded();
	DisableVADIfNeeded();
	StopRecognizerForEndPlayOrError();

	ClearRuntimeReferences();

	Super::EndPlay(EndPlayReason);
}

// -----------------------------------------------------------------------------
// Assistant state
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::HandleAssistantStateChanged(
	EAssistantState NewState,
	EAssistantState PreviousState
)
{
	if (bShuttingDown)
	{
		return;
	}

	switch (NewState)
	{
	case EAssistantState::Listening:
	case EAssistantState::WaitingForFollowUp:
		StartListeningAutomatic();
		break;

	case EAssistantState::PersonDetected:
	case EAssistantState::Greeting:
	case EAssistantState::ProcessingSpeech:
	case EAssistantState::QueryingAI:
	case EAssistantState::Speaking:
		CancelCurrentCapture();
		break;

	case EAssistantState::Idle:
	case EAssistantState::EndingInteraction:
	case EAssistantState::Error:
	default:
		ResetSpeechSession();
		break;
	}
}

bool AAssistantSpeechRecognizerActor::IsListeningState(
	EAssistantState State
) const
{
	return State == EAssistantState::Listening
		|| State == EAssistantState::WaitingForFollowUp;
}

EAssistantState
AAssistantSpeechRecognizerActor::GetCurrentAssistantStateSafe() const
{
	if (!AssistantSubsystem)
	{
		return EAssistantState::Idle;
	}

	return AssistantSubsystem->GetCurrentState();
}

// -----------------------------------------------------------------------------
// Recognizer initialization
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::InitializeSpeechRecognizer()
{
	if (bShuttingDown)
	{
		return;
	}

	if (bRecognizerReady || bIsStartingRecognition)
	{
		return;
	}

	bIsStartingRecognition = true;

	SpeechRecognizer = USpeechRecognizer::CreateSpeechRecognizer();

	if (!SpeechRecognizer)
	{
		bIsStartingRecognition = false;
		bRecognizerReady = false;

		DebugLog(
			TEXT("InitializeSpeechRecognizer: creazione recognizer fallita.")
		);

		return;
	}

	SpeechRecognizer->SetNonStreamingDefaults();
	SpeechRecognizer->SetLanguage(ESpeechRecognizerLanguage::It);
	SpeechRecognizer->SetTranslateToEnglish(false);
	SpeechRecognizer->SetSuppressBlank(true);
	SpeechRecognizer->SetSuppressNonSpeechTokens(true);
	SpeechRecognizer->SetNumOfThreads(0);

	SpeechRecognizer->OnRecognizedTextSegment.AddDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleRecognizedTextSegment
	);

	SpeechRecognizer->OnRecognitionFinished.AddDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleRecognitionFinished
	);

	SpeechRecognizer->OnRecognitionError.AddDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleRecognitionError
	);

	FOnSpeechRecognitionStartedDynamic OnStarted;

	OnStarted.BindDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleStartSpeechRecognition
	);

	SpeechRecognizer->StartSpeechRecognition(OnStarted);

	DebugLog(
		TEXT("InitializeSpeechRecognizer: StartSpeechRecognition chiamato.")
	);
}

void AAssistantSpeechRecognizerActor::HandleStartSpeechRecognition(
	bool bSucceeded
)
{
	bIsStartingRecognition = false;
	bRecognizerReady = bSucceeded;

	if (!bSucceeded)
	{
		DebugLog(TEXT("Avvio SpeechRecognizer fallito."));

		SpeechRecognizer = nullptr;
		return;
	}

	DebugLog(TEXT("SpeechRecognizer pronto."));

	const bool bShouldStartListening =
		bPendingAutomaticListen
		|| IsListeningState(GetCurrentAssistantStateSafe());

	bPendingAutomaticListen = false;

	if (bShouldStartListening)
	{
		StartListeningAutomatic();
	}
}

// -----------------------------------------------------------------------------
// Automatic listening
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::StartListeningAutomatic()
{
	if (bShuttingDown || !bAutomaticListeningEnabled)
	{
		return;
	}

	const EAssistantState CurrentState =
		GetCurrentAssistantStateSafe();

	if (!IsListeningState(CurrentState))
	{
		return;
	}

	if (!bRecognizerReady || !SpeechRecognizer)
	{
		bPendingAutomaticListen = true;

		if (!bIsStartingRecognition)
		{
			InitializeSpeechRecognizer();
		}

		return;
	}

	if (bIsRecording || bIsFinalizingSpeech)
	{
		return;
	}

	ClearMaximumUtteranceTimer();

	bPendingAutomaticListen = false;
	bDiscardRecognitionResult = false;
	bSpeechDetected = false;
	bIsFinalizingSpeech = false;
	AccumulatedText.Empty();

	CapturableSoundWave =
		UCapturableSoundWave::CreateCapturableSoundWave();

	if (!CapturableSoundWave)
	{
		DebugLog(
			TEXT("CreateCapturableSoundWave ha restituito nullptr.")
		);

		return;
	}

	bVADEnabled = CapturableSoundWave->ToggleVAD(true);

	if (!bVADEnabled)
	{
		DebugLog(TEXT("Impossibile abilitare il VAD."));

		CapturableSoundWave = nullptr;
		return;
	}

	CapturableSoundWave->SetVADMode(
		ERuntimeVADMode::VeryAggressive
	);

	CapturableSoundWave->SetMinimumSpeechDuration(
		MinimumSpeechDurationMs
	);

	CapturableSoundWave->SetSilenceDuration(
		SilenceDurationMs
	);

	CapturableSoundWave->OnSpeechStarted.AddDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleVADSpeechStarted
	);

	CapturableSoundWave->OnSpeechEnded.AddDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleVADSpeechEnded
	);

	bIsRecording = true;

	const bool bCaptureStarted =
		CapturableSoundWave->StartCapture(MicrophoneDeviceId);

	if (!bCaptureStarted)
	{
		bIsRecording = false;

		DebugLog(TEXT("StartCapture fallito."));

		DisableVADIfNeeded();
		CapturableSoundWave = nullptr;

		return;
	}

	DebugLog(TEXT("AUTO LISTENING STARTED"));
}

// -----------------------------------------------------------------------------
// VAD
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::HandleVADSpeechStarted()
{
	if (!bIsRecording || bIsFinalizingSpeech)
	{
		return;
	}

	if (bSpeechDetected)
	{
		return;
	}

	bSpeechDetected = true;

	DebugLog(TEXT("VAD: SPEECH STARTED"));

	ClearMaximumUtteranceTimer();

	if (MaximumUtteranceDurationSeconds > 0.0f)
	{
		GetWorldTimerManager().SetTimer(
			MaximumUtteranceTimerHandle,
			this,
			&AAssistantSpeechRecognizerActor::HandleMaximumUtteranceTimeout,
			MaximumUtteranceDurationSeconds,
			false
		);

		DebugLog(
			FString::Printf(
				TEXT("VAD WATCHDOG STARTED: %.1f secondi"),
				MaximumUtteranceDurationSeconds
			)
		);
	}
}

void AAssistantSpeechRecognizerActor::HandleVADSpeechEnded()
{
	if (!bIsRecording)
	{
		return;
	}

	if (!bSpeechDetected)
	{
		return;
	}

	if (bIsFinalizingSpeech)
	{
		return;
	}

	ClearMaximumUtteranceTimer();

	bIsFinalizingSpeech = true;

	DebugLog(TEXT("VAD: SPEECH ENDED"));

	FinalizeCurrentUtterance();
}

// -----------------------------------------------------------------------------
// Maximum utterance timeout
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::HandleMaximumUtteranceTimeout()
{
	ClearMaximumUtteranceTimer();

	if (bShuttingDown)
	{
		return;
	}

	if (!bIsRecording)
	{
		return;
	}

	if (!bSpeechDetected)
	{
		return;
	}

	if (bIsFinalizingSpeech)
	{
		return;
	}

	bIsFinalizingSpeech = true;

	DebugLog(
		FString::Printf(
			TEXT("VAD: MAXIMUM UTTERANCE TIMEOUT DOPO %.1f SECONDI"),
			MaximumUtteranceDurationSeconds
		)
	);

	FinalizeCurrentUtterance();
}

void AAssistantSpeechRecognizerActor::ClearMaximumUtteranceTimer()
{
	if (!GetWorld())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(MaximumUtteranceTimerHandle);
}

// -----------------------------------------------------------------------------
// PCM extraction and Whisper processing
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::FinalizeCurrentUtterance()
{
	ClearMaximumUtteranceTimer();

	if (!CapturableSoundWave)
	{
		bIsFinalizingSpeech = false;
		bSpeechDetected = false;

		return;
	}

	if (!bIsRecording)
	{
		bIsFinalizingSpeech = false;
		bSpeechDetected = false;

		return;
	}

	CapturableSoundWave->StopCapture();
	bIsRecording = false;

	TArray<float> PCMBuffer =
		CapturableSoundWave->GetPCMBufferCopy();

	const int32 SampleRate =
		CapturableSoundWave->GetSampleRate();

	const int32 NumOfChannels =
		CapturableSoundWave->GetNumOfChannels();

	if (PCMBuffer.IsEmpty())
	{
		DiscardCurrentUtteranceAndRestart(
			TEXT("STT: PCM BUFFER VUOTO")
		);

		return;
	}

	if (SampleRate <= 0 || NumOfChannels <= 0)
	{
		DiscardCurrentUtteranceAndRestart(
			FString::Printf(
				TEXT(
					"STT: formato PCM non valido. "
					"SampleRate=%d Channels=%d"
				),
				SampleRate,
				NumOfChannels
			)
		);

		return;
	}

	float Rms = 0.0f;
	float Peak = 0.0f;

	const bool bValidSpeechAudio =
		IsPcmAudioValidForSpeech(PCMBuffer, Rms, Peak);

	DebugLog(
		FString::Printf(
			TEXT(
				"STT AUDIO ENERGY | RMS=%.6f Peak=%.6f "
				"MinRMS=%.6f MinPeak=%.6f"
			),
			Rms,
			Peak,
			MinimumValidSpeechRms,
			MinimumValidSpeechPeak
		)
	);

	if (!bValidSpeechAudio)
	{
		DiscardCurrentUtteranceAndRestart(
			TEXT("STT: audio scartato perché sotto soglia energetica.")
		);

		return;
	}

	if (!SpeechRecognizer || !bRecognizerReady)
	{
		DiscardCurrentUtteranceAndRestart(
			TEXT("SpeechRecognizer non valido o non pronto.")
		);

		return;
	}

	bDiscardRecognitionResult = false;

	const int32 NumSamples = PCMBuffer.Num();

	SpeechRecognizer->ProcessAudioData(
		MoveTemp(PCMBuffer),
		static_cast<float>(SampleRate),
		NumOfChannels,
		true
	);

	DebugLog(
		FString::Printf(
			TEXT(
				"STT: AUDIO INVIATO | "
				"Samples=%d Rate=%d Channels=%d"
			),
			NumSamples,
			SampleRate,
			NumOfChannels
		)
	);

	DisableVADIfNeeded();
}

bool AAssistantSpeechRecognizerActor::IsPcmAudioValidForSpeech(
	const TArray<float>& PCMBuffer,
	float& OutRms,
	float& OutPeak
) const
{
	OutRms = 0.0f;
	OutPeak = 0.0f;

	if (PCMBuffer.IsEmpty())
	{
		return false;
	}

	double SumSquares = 0.0;

	for (const float Sample : PCMBuffer)
	{
		const float AbsSample = FMath::Abs(Sample);

		OutPeak = FMath::Max(OutPeak, AbsSample);
		SumSquares += static_cast<double>(Sample) * Sample;
	}

	OutRms = FMath::Sqrt(
		static_cast<float>(SumSquares / PCMBuffer.Num())
	);

	return OutRms >= MinimumValidSpeechRms
		&& OutPeak >= MinimumValidSpeechPeak;
}

void AAssistantSpeechRecognizerActor::DiscardCurrentUtteranceAndRestart(
	const FString& Reason
)
{
	DebugLog(Reason);

	bIsFinalizingSpeech = false;
	bSpeechDetected = false;
	bDiscardRecognitionResult = false;
	bIsRecording = false;

	AccumulatedText.Empty();

	DisableVADIfNeeded();
	CapturableSoundWave = nullptr;

	RestartListeningIfAllowed();
}

// -----------------------------------------------------------------------------
// Whisper delegates
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::HandleRecognizedTextSegment(
	const FString& RecognizedWords
)
{
	if (bDiscardRecognitionResult)
	{
		return;
	}

	const FString CleanSegment =
		RecognizedWords.TrimStartAndEnd();

	if (CleanSegment.IsEmpty())
	{
		return;
	}

	if (AccumulatedText.IsEmpty())
	{
		AccumulatedText = CleanSegment;
	}
	else
	{
		AccumulatedText += TEXT(" ");
		AccumulatedText += CleanSegment;
	}

	DebugLog(
		FString::Printf(
			TEXT("STT SEGMENT: %s"),
			*CleanSegment
		)
	);
}

void AAssistantSpeechRecognizerActor::HandleRecognitionFinished()
{
	if (bDiscardRecognitionResult)
	{
		ResetAfterNoTextOrDiscard();
		RestartListeningIfAllowed();

		return;
	}

	const FString FinalText =
		AccumulatedText.TrimStartAndEnd();

	if (FinalText.IsEmpty())
	{
		DebugLog(TEXT("STT: NESSUN TESTO RICONOSCIUTO"));

		ResetAfterNoTextOrDiscard();
		RestartListeningIfAllowed();

		return;
	}

	if (IsLikelyWhisperHallucination(FinalText))
	{
		DebugLog(
			FString::Printf(
				TEXT("STT SCARTATO COME ALLUCINAZIONE: %s"),
				*FinalText
			)
		);

		ResetAfterNoTextOrDiscard();
		RestartListeningIfAllowed();

		return;
	}

	SubmitFinalText(FinalText);
}

bool AAssistantSpeechRecognizerActor::IsLikelyWhisperHallucination(
	const FString& FinalText
) const
{
	FString CleanText = FinalText.TrimStartAndEnd().ToLower();

	CleanText.ReplaceInline(TEXT("."), TEXT(""));
	CleanText.ReplaceInline(TEXT("!"), TEXT(""));
	CleanText.ReplaceInline(TEXT("?"), TEXT(""));
	CleanText.ReplaceInline(TEXT(","), TEXT(""));

	if (CleanText.Contains(TEXT("sottotitoli")))
	{
		return true;
	}

	if (CleanText.Contains(TEXT("amaraorg")))
	{
		return true;
	}

	if (CleanText.Contains(TEXT("amara org")))
	{
		return true;
	}

	if (CleanText.Contains(TEXT("grazie per la visione")))
	{
		return true;
	}

	if (CleanText.Contains(TEXT("a cura di")))
	{
		return true;
	}

	return false;
}

void AAssistantSpeechRecognizerActor::HandleRecognitionError(
	const FString& ShortErrorMessage,
	const FString& LongErrorMessage
)
{
	DebugLog(
		FString::Printf(
			TEXT("STT ERROR: %s | %s"),
			*ShortErrorMessage,
			*LongErrorMessage
		)
	);

	ClearMaximumUtteranceTimer();

	StopCaptureIfNeeded();
	DisableVADIfNeeded();

	bIsRecording = false;
	bSpeechDetected = false;
	bIsFinalizingSpeech = false;
	bDiscardRecognitionResult = false;
	bPendingAutomaticListen = false;

	AccumulatedText.Empty();

	CapturableSoundWave = nullptr;

	StopRecognizerForEndPlayOrError();

	bRecognizerReady = false;
	bIsStartingRecognition = false;
	SpeechRecognizer = nullptr;

	RetryRecognitionAfterError();
}

// -----------------------------------------------------------------------------
// Submit and restart
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::SubmitFinalText(
	const FString& FinalText
)
{
	ClearMaximumUtteranceTimer();

	bIsFinalizingSpeech = false;
	bSpeechDetected = false;
	bIsRecording = false;
	bDiscardRecognitionResult = false;

	CapturableSoundWave = nullptr;

	DebugLog(
		FString::Printf(
			TEXT("STT FINAL: %s"),
			*FinalText
		)
	);

	if (!AssistantSubsystem)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			AssistantSubsystem =
				GameInstance->GetSubsystem<UAssistantSubsystem>();
		}
	}

	if (AssistantSubsystem)
	{
		AssistantSubsystem->SubmitUserText(FinalText);
	}
	else
	{
		DebugLog(TEXT("SubmitFinalText: AssistantSubsystem non valido."));
	}

	AccumulatedText.Empty();
}

void AAssistantSpeechRecognizerActor::RestartListeningIfAllowed()
{
	if (bShuttingDown)
	{
		return;
	}

	if (IsListeningState(GetCurrentAssistantStateSafe()))
	{
		StartListeningAutomatic();
	}
}

void AAssistantSpeechRecognizerActor::RetryRecognitionAfterError()
{
	if (bShuttingDown)
	{
		return;
	}

	if (!IsListeningState(GetCurrentAssistantStateSafe()))
	{
		return;
	}

	bPendingAutomaticListen = true;

	GetWorldTimerManager().ClearTimer(RetryRecognitionTimerHandle);

	GetWorldTimerManager().SetTimer(
		RetryRecognitionTimerHandle,
		this,
		&AAssistantSpeechRecognizerActor::InitializeSpeechRecognizer,
		0.5f,
		false
	);
}

void AAssistantSpeechRecognizerActor::ResetAfterNoTextOrDiscard()
{
	ClearMaximumUtteranceTimer();

	bDiscardRecognitionResult = false;
	bIsFinalizingSpeech = false;
	bSpeechDetected = false;
	bIsRecording = false;

	AccumulatedText.Empty();

	DisableVADIfNeeded();
	CapturableSoundWave = nullptr;
}

// -----------------------------------------------------------------------------
// Cancel and reset
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::CancelCurrentCapture()
{
	bPendingAutomaticListen = false;

	ClearMaximumUtteranceTimer();

	StopCaptureIfNeeded();
	DisableVADIfNeeded();

	bIsRecording = false;
	bSpeechDetected = false;

	if (bIsFinalizingSpeech)
	{
		bDiscardRecognitionResult = true;
		AccumulatedText.Empty();

		return;
	}

	bDiscardRecognitionResult = false;
	bIsFinalizingSpeech = false;

	AccumulatedText.Empty();
	CapturableSoundWave = nullptr;
}

void AAssistantSpeechRecognizerActor::ResetSpeechSession()
{
	bPendingAutomaticListen = false;

	ClearMaximumUtteranceTimer();

	StopCaptureIfNeeded();
	DisableVADIfNeeded();

	bIsRecording = false;
	bSpeechDetected = false;

	if (bIsFinalizingSpeech)
	{
		bDiscardRecognitionResult = true;
		AccumulatedText.Empty();

		DebugLog(
			TEXT(
				"STT SESSION RESET: "
				"il risultato pendente sarà scartato."
			)
		);

		return;
	}

	bDiscardRecognitionResult = false;
	bIsFinalizingSpeech = false;

	AccumulatedText.Empty();
	CapturableSoundWave = nullptr;

	DebugLog(TEXT("STT SESSION RESET"));
}

// -----------------------------------------------------------------------------
// Low-level cleanup
// -----------------------------------------------------------------------------

void AAssistantSpeechRecognizerActor::StopCaptureIfNeeded()
{
	ClearMaximumUtteranceTimer();

	if (!CapturableSoundWave)
	{
		bIsRecording = false;
		return;
	}

	if (bIsRecording || CapturableSoundWave->IsCapturing())
	{
		CapturableSoundWave->StopCapture();
	}

	bIsRecording = false;
}

void AAssistantSpeechRecognizerActor::DisableVADIfNeeded()
{
	if (!CapturableSoundWave || !bVADEnabled)
	{
		return;
	}

	CapturableSoundWave->ResetVAD();
	CapturableSoundWave->ToggleVAD(false);

	bVADEnabled = false;
}

void AAssistantSpeechRecognizerActor::StopRecognizerForEndPlayOrError()
{
	if (!SpeechRecognizer)
	{
		return;
	}

	SpeechRecognizer->OnRecognizedTextSegment.RemoveDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleRecognizedTextSegment
	);

	SpeechRecognizer->OnRecognitionFinished.RemoveDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleRecognitionFinished
	);

	SpeechRecognizer->OnRecognitionError.RemoveDynamic(
		this,
		&AAssistantSpeechRecognizerActor::HandleRecognitionError
	);

	SpeechRecognizer->StopSpeechRecognition();
}

void AAssistantSpeechRecognizerActor::ClearRuntimeReferences()
{
	ClearMaximumUtteranceTimer();

	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(RetryRecognitionTimerHandle);
	}

	CapturableSoundWave = nullptr;
	SpeechRecognizer = nullptr;
	AssistantSubsystem = nullptr;

	AccumulatedText.Empty();

	bRecognizerReady = false;
	bIsStartingRecognition = false;
	bPendingAutomaticListen = false;

	bIsRecording = false;
	bSpeechDetected = false;
	bIsFinalizingSpeech = false;
	bDiscardRecognitionResult = false;

	bVADEnabled = false;
}

void AAssistantSpeechRecognizerActor::DebugLog(
	const FString& Message
) const
{
	if (!bPrintDebug)
	{
		return;
	}

	UE_LOG(
		LogAssistantSpeechRecognizerActor,
		Warning,
		TEXT("%s"),
		*Message
	);
}