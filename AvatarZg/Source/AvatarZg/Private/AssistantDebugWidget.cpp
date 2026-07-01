#include "AssistantDebugWidget.h"

#include "AssistantSubsystem.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"

void UAssistantDebugWidget::NativeConstruct()
{
	Super::NativeConstruct();

	BindAssistantSubsystem();

	if (Button_Start)
	{
		Button_Start->OnClicked.AddDynamic(this, &UAssistantDebugWidget::HandleStartClicked);
	}

	if (Button_Send)
	{
		Button_Send->OnClicked.AddDynamic(this, &UAssistantDebugWidget::HandleSendClicked);
	}

	if (Button_End)
	{
		Button_End->OnClicked.AddDynamic(this, &UAssistantDebugWidget::HandleEndClicked);
	}

	SetAnswerText(TEXT("Debug assistant pronto."));

	if (AssistantSubsystem)
	{
		SetStateText(AssistantSubsystem->GetCurrentState());
	}
	else
	{
		AppendLog(TEXT("[ERROR] AssistantSubsystem non trovato."));
	}
}

void UAssistantDebugWidget::NativeDestruct()
{
	if (Button_Start)
	{
		Button_Start->OnClicked.RemoveDynamic(this, &UAssistantDebugWidget::HandleStartClicked);
	}

	if (Button_Send)
	{
		Button_Send->OnClicked.RemoveDynamic(this, &UAssistantDebugWidget::HandleSendClicked);
	}

	if (Button_End)
	{
		Button_End->OnClicked.RemoveDynamic(this, &UAssistantDebugWidget::HandleEndClicked);
	}

	UnbindAssistantSubsystem();

	Super::NativeDestruct();
}

void UAssistantDebugWidget::BindAssistantSubsystem()
{
	if (!GetGameInstance())
	{
		return;
	}

	AssistantSubsystem = GetGameInstance()->GetSubsystem<UAssistantSubsystem>();

	if (!AssistantSubsystem)
	{
		return;
	}

	AssistantSubsystem->OnAssistantTextReady.AddDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantTextReady
	);

	AssistantSubsystem->OnAssistantResponseReady.AddDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantResponseReady
	);

	AssistantSubsystem->OnStateChanged.AddDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantStateChanged
	);

	AssistantSubsystem->OnAssistantError.AddDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantError
	);
}

void UAssistantDebugWidget::UnbindAssistantSubsystem()
{
	if (!AssistantSubsystem)
	{
		return;
	}

	AssistantSubsystem->OnAssistantTextReady.RemoveDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantTextReady
	);

	AssistantSubsystem->OnAssistantResponseReady.RemoveDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantResponseReady
	);

	AssistantSubsystem->OnStateChanged.RemoveDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantStateChanged
	);

	AssistantSubsystem->OnAssistantError.RemoveDynamic(
		this,
		&UAssistantDebugWidget::HandleAssistantError
	);

	AssistantSubsystem = nullptr;
}

void UAssistantDebugWidget::HandleStartClicked()
{
	if (!AssistantSubsystem)
	{
		AppendLog(TEXT("[ERROR] Impossibile avviare: AssistantSubsystem non valido."));
		return;
	}

	AppendLog(FString::Printf(
		TEXT("[CONFIG] BackendEndpointUrl = %s"),
		BackendEndpointUrl.IsEmpty() ? TEXT("<EMPTY>") : *BackendEndpointUrl
	));

	AppendLog(FString::Printf(
		TEXT("[CONFIG] MallId = %s"),
		*MallId
	));

	AssistantSubsystem->SetBackendEndpoint(BackendEndpointUrl);
	AssistantSubsystem->SetMallId(MallId);
	AssistantSubsystem->StartInteraction();

	AppendLog(TEXT("[USER ACTION] Start Interaction"));
}

void UAssistantDebugWidget::HandleSendClicked()
{
	if (!AssistantSubsystem)
	{
		AppendLog(TEXT("[ERROR] Impossibile inviare: AssistantSubsystem non valido."));
		return;
	}

	if (!Input_UserText)
	{
		AppendLog(TEXT("[ERROR] Input_UserText non collegato nel Widget Blueprint."));
		return;
	}

	const FString UserText = Input_UserText->GetText().ToString().TrimStartAndEnd();

	if (UserText.IsEmpty())
	{
		AppendLog(TEXT("[WARN] Testo utente vuoto."));
		return;
	}

	AppendLog(FString::Printf(TEXT("[USER] %s"), *UserText));

	AssistantSubsystem->SubmitUserText(UserText);

	Input_UserText->SetText(FText::GetEmpty());
}

void UAssistantDebugWidget::HandleEndClicked()
{
	if (!AssistantSubsystem)
	{
		AppendLog(TEXT("[ERROR] Impossibile terminare: AssistantSubsystem non valido."));
		return;
	}

	AssistantSubsystem->EndInteraction();

	AppendLog(TEXT("[USER ACTION] End Interaction"));
}

void UAssistantDebugWidget::HandleAssistantTextReady(FString TextToSay)
{
	SetAnswerText(TextToSay);

	AppendLog(FString::Printf(TEXT("[ASSISTANT TEXT] %s"), *TextToSay));

	if (bAutoNotifyFinishedSpeakingInDebug && AssistantSubsystem)
	{
		AssistantSubsystem->NotifyAssistantFinishedSpeaking();
	}
}

void UAssistantDebugWidget::HandleAssistantResponseReady(FAssistantResponse Response)
{
	AppendLog(FString::Printf(
		TEXT("[AI RESPONSE] Confidence: %s | NeedsClarification: %s | OutOfScope: %s"),
		*ConfidenceToString(Response.Confidence),
		Response.bNeedsClarification ? TEXT("true") : TEXT("false"),
		Response.bIsOutOfScope ? TEXT("true") : TEXT("false")
	));

	if (Response.ClarificationQuestion.IsEmpty() == false)
	{
		AppendLog(FString::Printf(
			TEXT("[CLARIFICATION] %s"),
			*Response.ClarificationQuestion
		));
	}

	if (Response.SuggestedShops.Num() > 0)
	{
		AppendLog(FString::Printf(
			TEXT("[SUGGESTED SHOPS] %d"),
			Response.SuggestedShops.Num()
		));

		for (const FAssistantShopSuggestion& Shop : Response.SuggestedShops)
		{
			AppendLog(FString::Printf(
				TEXT("- %s | %s | %s | %s"),
				*Shop.Name,
				*Shop.Category,
				*Shop.Floor,
				*Shop.Area
			));
		}
	}
}

void UAssistantDebugWidget::HandleAssistantStateChanged(
	EAssistantState NewState,
	EAssistantState PreviousState
)
{
	SetStateText(NewState);

	AppendLog(FString::Printf(
		TEXT("[STATE] %s -> %s"),
		*StateToString(PreviousState),
		*StateToString(NewState)
	));
}

void UAssistantDebugWidget::HandleAssistantError(FString ErrorMessage)
{
	AppendLog(FString::Printf(TEXT("[ERROR] %s"), *ErrorMessage));
	SetAnswerText(ErrorMessage);
}

void UAssistantDebugWidget::AppendLog(const FString& Line)
{
	if (Line.IsEmpty())
	{
		return;
	}

	if (!LogBuffer.IsEmpty())
	{
		LogBuffer.Append(TEXT("\n"));
	}

	LogBuffer.Append(Line);

	constexpr int32 MaxLogLength = 8000;

	if (LogBuffer.Len() > MaxLogLength)
	{
		LogBuffer = LogBuffer.Right(MaxLogLength);
	}

	if (Text_Log)
	{
		Text_Log->SetText(FText::FromString(LogBuffer));
	}

	UE_LOG(LogTemp, Log, TEXT("%s"), *Line);
}

void UAssistantDebugWidget::SetAnswerText(const FString& Text)
{
	if (Text_AssistantAnswer)
	{
		Text_AssistantAnswer->SetText(FText::FromString(Text));
	}
}

void UAssistantDebugWidget::SetStateText(EAssistantState State)
{
	if (Text_CurrentState)
	{
		Text_CurrentState->SetText(FText::FromString(StateToString(State)));
	}
}

FString UAssistantDebugWidget::StateToString(EAssistantState State) const
{
	const UEnum* EnumPtr = StaticEnum<EAssistantState>();

	if (!EnumPtr)
	{
		return TEXT("Unknown");
	}

	return EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(State)).ToString();
}

FString UAssistantDebugWidget::ConfidenceToString(EAssistantConfidence Confidence) const
{
	const UEnum* EnumPtr = StaticEnum<EAssistantConfidence>();

	if (!EnumPtr)
	{
		return TEXT("Unknown");
	}

	return EnumPtr->GetDisplayNameTextByValue(static_cast<int64>(Confidence)).ToString();
}