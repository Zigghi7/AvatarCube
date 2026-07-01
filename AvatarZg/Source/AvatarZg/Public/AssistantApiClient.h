#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HttpFwd.h"
#include "AssistantTypes.h"
#include "AssistantApiClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAssistantApiCompletedSignature,
	bool,
	bSuccess,
	FAssistantResponse,
	Response
);

UCLASS(BlueprintType)
class AVATARZG_API UAssistantApiClient : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintAssignable, Category = "Assistant|API")
	FAssistantApiCompletedSignature OnApiCompleted;

	UFUNCTION(BlueprintCallable, Category = "Assistant|API")
	void Configure(const FString& InEndpointUrl, float InTimeoutSeconds = 30.0f);

	UFUNCTION(BlueprintCallable, Category = "Assistant|API")
	void SendMessage(const FAssistantRequest& RequestPayload);

private:

	UPROPERTY()
	FString EndpointUrl;

	UPROPERTY()
	float TimeoutSeconds = 30.0f;

private:

	void SendMockResponse(const FAssistantRequest& RequestPayload);
	void HandleHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
};