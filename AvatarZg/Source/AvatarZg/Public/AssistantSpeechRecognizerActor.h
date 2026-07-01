#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

#include "AssistantSubsystem.h"

#include "AssistantSpeechRecognizerActor.generated.h"

class USpeechRecognizer;
class UCapturableSoundWave;

UCLASS(Blueprintable)
class AVATARZG_API AAssistantSpeechRecognizerActor : public AActor
{
	GENERATED_BODY()

public:
	AAssistantSpeechRecognizerActor();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|Configuration")
	bool bAutomaticListeningEnabled = true;

	/**
	 * -1 utilizza il dispositivo di input predefinito di Windows.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|Configuration")
	int32 MicrophoneDeviceId = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|VAD")
	int32 MinimumSpeechDurationMs = 250;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|VAD")
	int32 SilenceDurationMs = 800;

	/**
	 * Durata massima di una singola registrazione dopo SpeechStarted.
	 * Imposta 0 per disabilitare.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|VAD")
	float MaximumUtteranceDurationSeconds = 12.0f;

	/**
	 * RMS minimo richiesto per considerare valido il buffer audio.
	 * Serve a evitare che silenzio o rumore quasi nullo vengano inviati a Whisper.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|Validation")
	float MinimumValidSpeechRms = 0.0035f;

	/**
	 * Picco minimo richiesto per considerare valido il buffer audio.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|Validation")
	float MinimumValidSpeechPeak = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant Speech|Debug")
	bool bPrintDebug = true;

	UFUNCTION(BlueprintCallable, Category = "Assistant Speech")
	void InitializeSpeechRecognizer();

	UFUNCTION(BlueprintCallable, Category = "Assistant Speech")
	void StartListeningAutomatic();

	UFUNCTION(BlueprintCallable, Category = "Assistant Speech")
	void CancelCurrentCapture();

	UFUNCTION(BlueprintCallable, Category = "Assistant Speech")
	void ResetSpeechSession();

private:
	UPROPERTY()
	TObjectPtr<UAssistantSubsystem> AssistantSubsystem = nullptr;

	UPROPERTY()
	TObjectPtr<USpeechRecognizer> SpeechRecognizer = nullptr;

	UPROPERTY()
	TObjectPtr<UCapturableSoundWave> CapturableSoundWave = nullptr;

	UPROPERTY()
	FString AccumulatedText;

	bool bRecognizerReady = false;
	bool bIsStartingRecognition = false;
	bool bPendingAutomaticListen = false;

	bool bIsRecording = false;
	bool bSpeechDetected = false;
	bool bIsFinalizingSpeech = false;
	bool bDiscardRecognitionResult = false;

	bool bVADEnabled = false;
	bool bShuttingDown = false;

	FTimerHandle RetryRecognitionTimerHandle;
	FTimerHandle MaximumUtteranceTimerHandle;

	UFUNCTION()
	void HandleAssistantStateChanged(
		EAssistantState NewState,
		EAssistantState PreviousState
	);

	UFUNCTION()
	void HandleStartSpeechRecognition(bool bSucceeded);

	UFUNCTION()
	void HandleRecognizedTextSegment(const FString& RecognizedWords);

	UFUNCTION()
	void HandleRecognitionFinished();

	UFUNCTION()
	void HandleRecognitionError(
		const FString& ShortErrorMessage,
		const FString& LongErrorMessage
	);

	UFUNCTION()
	void HandleVADSpeechStarted();

	UFUNCTION()
	void HandleVADSpeechEnded();

	void HandleMaximumUtteranceTimeout();
	void ClearMaximumUtteranceTimer();

	void FinalizeCurrentUtterance();

	void RestartListeningIfAllowed();
	void RetryRecognitionAfterError();

	void ResetAfterNoTextOrDiscard();
	void SubmitFinalText(const FString& FinalText);

	bool IsPcmAudioValidForSpeech(
		const TArray<float>& PCMBuffer,
		float& OutRms,
		float& OutPeak
	) const;

	bool IsLikelyWhisperHallucination(const FString& FinalText) const;

	void DiscardCurrentUtteranceAndRestart(const FString& Reason);

	void StopCaptureIfNeeded();
	void DisableVADIfNeeded();
	void StopRecognizerForEndPlayOrError();
	void ClearRuntimeReferences();

	bool IsListeningState(EAssistantState State) const;
	EAssistantState GetCurrentAssistantStateSafe() const;

	void DebugLog(const FString& Message) const;
};