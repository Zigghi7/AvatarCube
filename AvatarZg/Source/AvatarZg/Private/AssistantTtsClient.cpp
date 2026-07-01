#include "AssistantTtsClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void UAssistantTtsClient::Configure(const FString& InTtsEndpointUrl, float InTimeoutSeconds)
{
	TtsEndpointUrl = InTtsEndpointUrl.TrimStartAndEnd();
	TimeoutSeconds = FMath::Max(5.0f, InTimeoutSeconds);

	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] Configure TtsEndpointUrl = '%s'"), *TtsEndpointUrl);
}

void UAssistantTtsClient::SynthesizeText(const FString& TextToSay)
{
	const FString CleanText = TextToSay.TrimStartAndEnd();

	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] SynthesizeText called. Endpoint = '%s'"), *TtsEndpointUrl);

	if (TtsEndpointUrl.IsEmpty())
	{
		const FString ErrorMessage = TEXT("TTS endpoint URL is empty.");
		UE_LOG(LogTemp, Error, TEXT("[AssistantTtsClient] %s"), *ErrorMessage);
		OnTtsError.Broadcast(ErrorMessage);
		OnTtsCompleted.Broadcast(false, TArray<uint8>());
		return;
	}

	if (CleanText.IsEmpty())
	{
		const FString ErrorMessage = TEXT("TextToSay is empty.");
		UE_LOG(LogTemp, Error, TEXT("[AssistantTtsClient] %s"), *ErrorMessage);
		OnTtsError.Broadcast(ErrorMessage);
		OnTtsCompleted.Broadcast(false, TArray<uint8>());
		return;
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetStringField(TEXT("text"), CleanText);

	FString RequestJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestJson);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] Request JSON = %s"), *RequestJson);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();

	HttpRequest->SetURL(TtsEndpointUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("audio/mpeg"));
	HttpRequest->SetContentAsString(RequestJson);
	HttpRequest->SetTimeout(TimeoutSeconds);

	HttpRequest->OnProcessRequestComplete().BindUObject(
		this,
		&UAssistantTtsClient::HandleHttpResponse
	);

	const bool bStarted = HttpRequest->ProcessRequest();

	if (!bStarted)
	{
		const FString ErrorMessage = TEXT("Failed to start TTS HTTP request.");
		UE_LOG(LogTemp, Error, TEXT("[AssistantTtsClient] %s"), *ErrorMessage);
		OnTtsError.Broadcast(ErrorMessage);
		OnTtsCompleted.Broadcast(false, TArray<uint8>());
	}
}

void UAssistantTtsClient::HandleHttpResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful
)
{
	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] HandleHttpResponse bWasSuccessful = %s"),
		bWasSuccessful ? TEXT("true") : TEXT("false")
	);

	if (!bWasSuccessful || !Response.IsValid())
	{
		const FString ErrorMessage = TEXT("Invalid TTS HTTP response.");
		UE_LOG(LogTemp, Error, TEXT("[AssistantTtsClient] %s"), *ErrorMessage);
		OnTtsError.Broadcast(ErrorMessage);
		OnTtsCompleted.Broadcast(false, TArray<uint8>());
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	const FString ContentType = Response->GetHeader(TEXT("Content-Type"));
	const TArray<uint8>& Content = Response->GetContent();

	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] HTTP StatusCode = %d"), StatusCode);
	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] Content-Type = %s"), *ContentType);
	UE_LOG(LogTemp, Warning, TEXT("[AssistantTtsClient] Audio bytes = %d"), Content.Num());

	if (StatusCode < 200 || StatusCode >= 300)
	{
		const FString ErrorBody = Response->GetContentAsString();

		const FString ErrorMessage = FString::Printf(
			TEXT("TTS backend returned HTTP %d. Body: %s"),
			StatusCode,
			*ErrorBody
		);

		UE_LOG(LogTemp, Error, TEXT("[AssistantTtsClient] %s"), *ErrorMessage);
		OnTtsError.Broadcast(ErrorMessage);
		OnTtsCompleted.Broadcast(false, TArray<uint8>());
		return;
	}

	if (Content.Num() <= 0)
	{
		const FString ErrorMessage = TEXT("TTS backend returned empty audio content.");
		UE_LOG(LogTemp, Error, TEXT("[AssistantTtsClient] %s"), *ErrorMessage);
		OnTtsError.Broadcast(ErrorMessage);
		OnTtsCompleted.Broadcast(false, TArray<uint8>());
		return;
	}

	OnTtsCompleted.Broadcast(true, Content);
}