#include "AssistantSubsystem.h"

#include "Engine/World.h"
#include "Misc/Guid.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssistantSubsystem, Log, All);

void UAssistantSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ApiClient = NewObject<UAssistantApiClient>(this);

	if (ApiClient)
	{
		ApiClient->Configure(BackendEndpointUrl);
		ApiClient->OnApiCompleted.AddDynamic(
			this,
			&UAssistantSubsystem::HandleApiCompleted
		);
	}

	ResetSession();
	SetState(EAssistantState::Idle);
}

void UAssistantSubsystem::Deinitialize()
{
	ClearConversationInactivityTimer();

	if (ApiClient)
	{
		ApiClient->OnApiCompleted.RemoveDynamic(
			this,
			&UAssistantSubsystem::HandleApiCompleted
		);

		ApiClient = nullptr;
	}

	ConversationHistory.Empty();

	Super::Deinitialize();
}

void UAssistantSubsystem::SetBackendEndpoint(
	const FString& InEndpointUrl
)
{
	BackendEndpointUrl = InEndpointUrl;

	if (ApiClient)
	{
		ApiClient->Configure(BackendEndpointUrl);
	}
}

void UAssistantSubsystem::SetMallId(const FString& InMallId)
{
	const FString CleanMallId = InMallId.TrimStartAndEnd();

	if (CleanMallId.IsEmpty())
	{
		OnAssistantError.Broadcast(
			TEXT("MallId non può essere vuoto.")
		);

		return;
	}

	MallId = CleanMallId;
}

void UAssistantSubsystem::SetConversationInactivityTimeout(
	float InTimeoutSeconds
)
{
	ConversationInactivityTimeoutSeconds =
		FMath::Max(0.0f, InTimeoutSeconds);

	if (IsWaitingForUserInputState(CurrentState))
	{
		StartConversationInactivityTimerIfNeeded();
	}
}

void UAssistantSubsystem::StartInteraction()
{
	if (bInteractionActive)
	{
		return;
	}

	ResetSession();

	bInteractionActive = true;
	SetState(EAssistantState::Greeting);

	const FString GreetingText =
		TEXT("Ciao, sono l'assistente virtuale del centro commerciale. Come posso aiutarti?");

	AddToConversationHistory(TEXT("assistant"), GreetingText);

	OnAssistantTextReady.Broadcast(GreetingText);
}

void UAssistantSubsystem::SubmitUserText(const FString& UserText)
{
	const FString CleanUserText = UserText.TrimStartAndEnd();

	if (CleanUserText.IsEmpty())
	{
		OnAssistantError.Broadcast(
			TEXT("Il testo dell'utente è vuoto.")
		);

		return;
	}

	ClearConversationInactivityTimer();

	if (!bInteractionActive)
	{
		bInteractionActive = true;
	}

	if (CurrentSessionId.IsEmpty())
	{
		CurrentSessionId = GenerateSessionId();
	}

	AddToConversationHistory(TEXT("user"), CleanUserText);

	SetState(EAssistantState::QueryingAI);

	FAssistantRequest RequestPayload;
	RequestPayload.SessionId = CurrentSessionId;
	RequestPayload.MallId = MallId;
	RequestPayload.UserText = CleanUserText;
	RequestPayload.Language = TEXT("it");
	RequestPayload.InteractionSource = TEXT("debug_text");
	RequestPayload.ConversationHistory = ConversationHistory;

	if (!ApiClient)
	{
		FAssistantResponse ErrorResponse;
		ErrorResponse.AnswerToSay =
			TEXT("Mi dispiace, il client dell'assistente non è inizializzato.");
		ErrorResponse.Confidence = EAssistantConfidence::Low;
		ErrorResponse.DebugInfo = TEXT("ApiClient is null.");

		HandleApiCompleted(false, ErrorResponse);
		return;
	}

	ApiClient->SendMessage(RequestPayload);
}

void UAssistantSubsystem::NotifyAssistantFinishedSpeaking()
{
	if (!bInteractionActive)
	{
		SetState(EAssistantState::Idle);
		return;
	}

	if (CurrentState == EAssistantState::Greeting)
	{
		SetState(EAssistantState::Listening);
		return;
	}

	if (CurrentState == EAssistantState::Speaking)
	{
		SetState(EAssistantState::WaitingForFollowUp);
		return;
	}

	if (CurrentState == EAssistantState::EndingInteraction)
	{
		bInteractionActive = false;
		SetState(EAssistantState::Idle);
	}
}

void UAssistantSubsystem::EndInteraction()
{
	ClearConversationInactivityTimer();

	if (!bInteractionActive)
	{
		SetState(EAssistantState::Idle);
		return;
	}

	SetState(EAssistantState::EndingInteraction);

	const FString GoodbyeText =
		TEXT("Grazie, è stato un piacere aiutarti. A presto.");

	AddToConversationHistory(TEXT("assistant"), GoodbyeText);

	OnAssistantTextReady.Broadcast(GoodbyeText);
}

void UAssistantSubsystem::ResetSession()
{
	ClearConversationInactivityTimer();

	CurrentSessionId = GenerateSessionId();
	ConversationHistory.Empty();
	LastResponse = FAssistantResponse();
}

EAssistantState UAssistantSubsystem::GetCurrentState() const
{
	return CurrentState;
}

FString UAssistantSubsystem::GetCurrentSessionId() const
{
	return CurrentSessionId;
}

bool UAssistantSubsystem::IsInteractionActive() const
{
	return bInteractionActive;
}

void UAssistantSubsystem::HandleApiCompleted(
	bool bSuccess,
	FAssistantResponse Response
)
{
	LastResponse = Response;

	if (!bSuccess)
	{
		SetState(EAssistantState::Error);

		const FString ErrorText =
			Response.AnswerToSay.IsEmpty()
			? TEXT("Mi dispiace, si è verificato un errore.")
			: Response.AnswerToSay;

		OnAssistantError.Broadcast(ErrorText);
		OnAssistantTextReady.Broadcast(ErrorText);

		AddToConversationHistory(TEXT("assistant"), ErrorText);

		return;
	}

	FString TextToSay = Response.AnswerToSay;

	if (TextToSay.IsEmpty())
	{
		TextToSay =
			TEXT("Mi dispiace, non ho trovato una risposta utile.");

		Response.AnswerToSay = TextToSay;
		Response.Confidence = EAssistantConfidence::Low;
	}

	AddToConversationHistory(TEXT("assistant"), TextToSay);

	OnAssistantResponseReady.Broadcast(Response);

	SetState(EAssistantState::Speaking);

	OnAssistantTextReady.Broadcast(TextToSay);
}

void UAssistantSubsystem::SetState(EAssistantState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	const EAssistantState PreviousState = CurrentState;
	CurrentState = NewState;

	OnStateChanged.Broadcast(CurrentState, PreviousState);

	if (IsWaitingForUserInputState(CurrentState))
	{
		StartConversationInactivityTimerIfNeeded();
	}
	else
	{
		ClearConversationInactivityTimer();
	}
}

bool UAssistantSubsystem::IsWaitingForUserInputState(
	EAssistantState State
) const
{
	return State == EAssistantState::Listening
		|| State == EAssistantState::WaitingForFollowUp;
}

void UAssistantSubsystem::StartConversationInactivityTimerIfNeeded()
{
	ClearConversationInactivityTimer();

	if (!bInteractionActive)
	{
		return;
	}

	if (ConversationInactivityTimeoutSeconds <= 0.0f)
	{
		return;
	}

	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		ConversationInactivityTimerHandle,
		this,
		&UAssistantSubsystem::HandleConversationInactivityTimeout,
		ConversationInactivityTimeoutSeconds,
		false
	);

	UE_LOG(
		LogAssistantSubsystem,
		Warning,
		TEXT("[ASSISTANT INACTIVITY] Timer started: %.1f seconds"),
		ConversationInactivityTimeoutSeconds
	);
}

void UAssistantSubsystem::ClearConversationInactivityTimer()
{
	UWorld* World = GetWorld();

	if (!World)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(
		ConversationInactivityTimerHandle
	);
}

void UAssistantSubsystem::HandleConversationInactivityTimeout()
{
	ClearConversationInactivityTimer();

	if (!bInteractionActive)
	{
		return;
	}

	if (!IsWaitingForUserInputState(CurrentState))
	{
		return;
	}

	UE_LOG(
		LogAssistantSubsystem,
		Warning,
		TEXT("[ASSISTANT INACTIVITY] Timeout after %.1f seconds"),
		ConversationInactivityTimeoutSeconds
	);

	EndInteraction();
}

void UAssistantSubsystem::AddToConversationHistory(
	const FString& Speaker,
	const FString& Text
)
{
	const FString CleanText = Text.TrimStartAndEnd();

	if (CleanText.IsEmpty())
	{
		return;
	}

	const FString HistoryLine = FString::Printf(
		TEXT("%s: %s"),
		*Speaker,
		*CleanText
	);

	ConversationHistory.Add(HistoryLine);

	constexpr int32 MaxHistoryItems = 20;

	while (ConversationHistory.Num() > MaxHistoryItems)
	{
		ConversationHistory.RemoveAt(0);
	}
}

FString UAssistantSubsystem::GenerateSessionId() const
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}