#pragma once

#include "CoreMinimal.h"
#include "AssistantTypes.generated.h"

UENUM(BlueprintType)
enum class EAssistantState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	PersonDetected UMETA(DisplayName = "Person Detected"),
	Greeting UMETA(DisplayName = "Greeting"),
	Listening UMETA(DisplayName = "Listening"),
	ProcessingSpeech UMETA(DisplayName = "Processing Speech"),
	QueryingAI UMETA(DisplayName = "Querying AI"),
	Speaking UMETA(DisplayName = "Speaking"),
	WaitingForFollowUp UMETA(DisplayName = "Waiting For Follow Up"),
	EndingInteraction UMETA(DisplayName = "Ending Interaction"),
	Error UMETA(DisplayName = "Error")
};

UENUM(BlueprintType)
enum class EAssistantConfidence : uint8
{
	Low UMETA(DisplayName = "Low"),
	Medium UMETA(DisplayName = "Medium"),
	High UMETA(DisplayName = "High")
};

USTRUCT(BlueprintType)
struct AVATARZG_API FAssistantShopSuggestion
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString Category;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString Floor;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString Area;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString Notes;
};

USTRUCT(BlueprintType)
struct AVATARZG_API FAssistantRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString SessionId;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString MallId;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString UserText;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString Language = TEXT("it");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString InteractionSource = TEXT("debug_text");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	TArray<FString> ConversationHistory;
};

USTRUCT(BlueprintType)
struct AVATARZG_API FAssistantResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString AnswerToSay;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	EAssistantConfidence Confidence = EAssistantConfidence::Low;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	TArray<FString> UsedSources;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	TArray<FAssistantShopSuggestion> SuggestedShops;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	bool bNeedsClarification = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString ClarificationQuestion;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	bool bIsOutOfScope = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Assistant")
	FString DebugInfo;
};