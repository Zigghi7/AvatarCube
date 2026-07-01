#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AssistantTypes.h"
#include "AssistantDebugWidget.generated.h"

class UAssistantSubsystem;
class UButton;
class UEditableTextBox;
class UTextBlock;

UCLASS()
class AVATARZG_API UAssistantDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant|Debug")
	FString BackendEndpointUrl = TEXT("http://127.0.0.1:8010/assistant/query");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant|Debug")
	FString MallId = TEXT("mall_x");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assistant|Debug")
	bool bAutoNotifyFinishedSpeakingInDebug = true;

protected:

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> Input_UserText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Start;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_Send;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> Button_End;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_AssistantAnswer;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_CurrentState;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Text_Log;

private:

	UPROPERTY()
	TObjectPtr<UAssistantSubsystem> AssistantSubsystem;

	UPROPERTY()
	FString LogBuffer;

private:

	UFUNCTION()
	void HandleStartClicked();

	UFUNCTION()
	void HandleSendClicked();

	UFUNCTION()
	void HandleEndClicked();

	UFUNCTION()
	void HandleAssistantTextReady(FString TextToSay);

	UFUNCTION()
	void HandleAssistantResponseReady(FAssistantResponse Response);

	UFUNCTION()
	void HandleAssistantStateChanged(EAssistantState NewState, EAssistantState PreviousState);

	UFUNCTION()
	void HandleAssistantError(FString ErrorMessage);

private:

	void BindAssistantSubsystem();
	void UnbindAssistantSubsystem();

	void AppendLog(const FString& Line);
	void SetAnswerText(const FString& Text);
	void SetStateText(EAssistantState State);
	FString StateToString(EAssistantState State) const;
	FString ConfidenceToString(EAssistantConfidence Confidence) const;
};