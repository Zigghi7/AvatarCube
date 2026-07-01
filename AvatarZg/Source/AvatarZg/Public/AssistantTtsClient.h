#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HttpFwd.h"
#include "AssistantTtsClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAssistantTtsCompletedSignature,
	bool,
	bSuccess,
	const TArray<uint8>&,
	AudioBytes
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FAssistantTtsErrorSignature,
	FString,
	ErrorMessage
);

UCLASS(BlueprintType)
class AVATARZG_API UAssistantTtsClient : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintAssignable, Category = "Assistant|TTS")
	FAssistantTtsCompletedSignature OnTtsCompleted;

	UPROPERTY(BlueprintAssignable, Category = "Assistant|TTS")
	FAssistantTtsErrorSignature OnTtsError;

	UFUNCTION(BlueprintCallable, Category = "Assistant|TTS")
	void Configure(const FString& InTtsEndpointUrl, float InTimeoutSeconds = 90.0f);

	UFUNCTION(BlueprintCallable, Category = "Assistant|TTS")
	void SynthesizeText(const FString& TextToSay);

private:

	UPROPERTY()
	FString TtsEndpointUrl;

	UPROPERTY()
	float TimeoutSeconds = 90.0f;

private:

	void HandleHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
};