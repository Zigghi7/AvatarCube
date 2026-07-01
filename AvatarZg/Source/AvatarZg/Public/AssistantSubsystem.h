#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"

#include "AssistantTypes.h"
#include "AssistantApiClient.h"

#include "AssistantSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAssistantStateChangedSignature,
	EAssistantState,
	NewState,
	EAssistantState,
	PreviousState
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FAssistantTextReadySignature,
	FString,
	TextToSay
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FAssistantResponseReadySignature,
	FAssistantResponse,
	Response
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FAssistantErrorSignature,
	FString,
	ErrorMessage
);

UCLASS()
class AVATARZG_API UAssistantSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:

	UPROPERTY(BlueprintAssignable, Category = "Assistant")
	FAssistantStateChangedSignature OnStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Assistant")
	FAssistantTextReadySignature OnAssistantTextReady;

	UPROPERTY(BlueprintAssignable, Category = "Assistant")
	FAssistantResponseReadySignature OnAssistantResponseReady;

	UPROPERTY(BlueprintAssignable, Category = "Assistant")
	FAssistantErrorSignature OnAssistantError;

public:

	/**
	 * Timeout generale della conversazione quando l'assistente
	 * è in Listening o WaitingForFollowUp.
	 *
	 * Non dipende dal VAD. Un falso SpeechStarted non può cancellarlo.
	 *
	 * Imposta 0 per disabilitare.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant|Inactivity")
	float ConversationInactivityTimeoutSeconds = 20.0f;

public:

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void SetBackendEndpoint(const FString& InEndpointUrl);

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void SetMallId(const FString& InMallId);

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void SetConversationInactivityTimeout(float InTimeoutSeconds);

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void StartInteraction();

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void SubmitUserText(const FString& UserText);

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void NotifyAssistantFinishedSpeaking();

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void EndInteraction();

	UFUNCTION(BlueprintCallable, Category = "Assistant")
	void ResetSession();

	UFUNCTION(BlueprintPure, Category = "Assistant")
	EAssistantState GetCurrentState() const;

	UFUNCTION(BlueprintPure, Category = "Assistant")
	FString GetCurrentSessionId() const;

	UFUNCTION(BlueprintPure, Category = "Assistant")
	bool IsInteractionActive() const;

private:

	UPROPERTY()
	TObjectPtr<UAssistantApiClient> ApiClient;

	UPROPERTY()
	EAssistantState CurrentState = EAssistantState::Idle;

	UPROPERTY()
	FString BackendEndpointUrl;

	UPROPERTY()
	FString MallId = TEXT("mall_x");

	UPROPERTY()
	FString CurrentSessionId;

	UPROPERTY()
	bool bInteractionActive = false;

	UPROPERTY()
	TArray<FString> ConversationHistory;

	UPROPERTY()
	FAssistantResponse LastResponse;

	FTimerHandle ConversationInactivityTimerHandle;

private:

	UFUNCTION()
	void HandleApiCompleted(bool bSuccess, FAssistantResponse Response);

	void SetState(EAssistantState NewState);

	void StartConversationInactivityTimerIfNeeded();
	void ClearConversationInactivityTimer();
	void HandleConversationInactivityTimeout();

	bool IsWaitingForUserInputState(EAssistantState State) const;

	void AddToConversationHistory(const FString& Speaker, const FString& Text);

	FString GenerateSessionId() const;
};