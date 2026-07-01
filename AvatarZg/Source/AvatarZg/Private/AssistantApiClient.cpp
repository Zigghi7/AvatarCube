#include "AssistantApiClient.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"

void UAssistantApiClient::Configure(const FString& InEndpointUrl, float InTimeoutSeconds)
{
	EndpointUrl = InEndpointUrl.TrimStartAndEnd();
	TimeoutSeconds = FMath::Max(1.0f, InTimeoutSeconds);

	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] Configure EndpointUrl = '%s'"), *EndpointUrl);
}

void UAssistantApiClient::SendMessage(const FAssistantRequest& RequestPayload)
{
	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] SendMessage called. EndpointUrl = '%s'"), *EndpointUrl);

	if (EndpointUrl.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] EndpointUrl is EMPTY. USING LOCAL MOCK RESPONSE."));
		SendMockResponse(RequestPayload);
		return;
	}

	FString RequestJson;

	const bool bSerialized = FJsonObjectConverter::UStructToJsonObjectString(
		RequestPayload,
		RequestJson
	);

	if (!bSerialized)
	{
		FAssistantResponse ErrorResponse;
		ErrorResponse.AnswerToSay = TEXT("Mi dispiace, non riesco a preparare correttamente la richiesta.");
		ErrorResponse.Confidence = EAssistantConfidence::Low;
		ErrorResponse.DebugInfo = TEXT("Failed to serialize FAssistantRequest to JSON.");

		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] Failed to serialize request."));
		OnApiCompleted.Broadcast(false, ErrorResponse);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] HTTP POST URL = %s"), *EndpointUrl);
	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] Request JSON = %s"), *RequestJson);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();

	HttpRequest->SetURL(EndpointUrl);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	HttpRequest->SetContentAsString(RequestJson);
	HttpRequest->SetTimeout(TimeoutSeconds);

	HttpRequest->OnProcessRequestComplete().BindUObject(
		this,
		&UAssistantApiClient::HandleHttpResponse
	);

	const bool bStarted = HttpRequest->ProcessRequest();

	if (!bStarted)
	{
		FAssistantResponse ErrorResponse;
		ErrorResponse.AnswerToSay = TEXT("Mi dispiace, non riesco a contattare il servizio dell'assistente.");
		ErrorResponse.Confidence = EAssistantConfidence::Low;
		ErrorResponse.DebugInfo = TEXT("HTTP request failed to start.");

		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] HTTP request failed to start."));
		OnApiCompleted.Broadcast(false, ErrorResponse);
	}
}

void UAssistantApiClient::HandleHttpResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful
)
{
	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] HandleHttpResponse called. bWasSuccessful = %s"),
		bWasSuccessful ? TEXT("true") : TEXT("false")
	);

	if (!bWasSuccessful || !Response.IsValid())
	{
		FAssistantResponse ErrorResponse;
		ErrorResponse.AnswerToSay = TEXT("Mi dispiace, al momento non riesco a ricevere una risposta.");
		ErrorResponse.Confidence = EAssistantConfidence::Low;
		ErrorResponse.DebugInfo = TEXT("HTTP response invalid or request failed.");

		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] HTTP response invalid or request failed."));
		OnApiCompleted.Broadcast(false, ErrorResponse);
		return;
	}

	const int32 StatusCode = Response->GetResponseCode();
	const FString ResponseText = Response->GetContentAsString();

	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] HTTP StatusCode = %d"), StatusCode);
	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] Response Body = %s"), *ResponseText);

	if (StatusCode < 200 || StatusCode >= 300)
	{
		FAssistantResponse ErrorResponse;
		ErrorResponse.AnswerToSay = TEXT("Mi dispiace, il servizio dell'assistente ha restituito un errore.");
		ErrorResponse.Confidence = EAssistantConfidence::Low;
		ErrorResponse.DebugInfo = FString::Printf(
			TEXT("HTTP status code: %d. Body: %s"),
			StatusCode,
			*ResponseText
		);

		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] HTTP error status: %d"), StatusCode);
		OnApiCompleted.Broadcast(false, ErrorResponse);
		return;
	}

	FAssistantResponse ParsedResponse;

	const bool bParsed = FJsonObjectConverter::JsonObjectStringToUStruct<FAssistantResponse>(
		ResponseText,
		&ParsedResponse,
		0,
		0
	);

	if (!bParsed)
	{
		FAssistantResponse ErrorResponse;
		ErrorResponse.AnswerToSay = TEXT("Mi dispiace, ho ricevuto una risposta non valida.");
		ErrorResponse.Confidence = EAssistantConfidence::Low;
		ErrorResponse.DebugInfo = FString::Printf(
			TEXT("JSON parse failed. Body: %s"),
			*ResponseText
		);

		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] JSON parse failed. Body: %s"), *ResponseText);
		OnApiCompleted.Broadcast(false, ErrorResponse);
		return;
	}

	if (ParsedResponse.AnswerToSay.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] ParsedResponse.AnswerToSay is EMPTY."));

		ParsedResponse.AnswerToSay = TEXT("Mi dispiace, non ho trovato un'informazione utile per rispondere.");
		ParsedResponse.Confidence = EAssistantConfidence::Low;
	}

	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] Parsed AnswerToSay = %s"), *ParsedResponse.AnswerToSay);
	UE_LOG(LogTemp, Warning, TEXT("[AssistantApiClient] Parsed SuggestedShops Num = %d"), ParsedResponse.SuggestedShops.Num());

	OnApiCompleted.Broadcast(true, ParsedResponse);
}

void UAssistantApiClient::SendMockResponse(const FAssistantRequest& RequestPayload)
{
	UE_LOG(LogTemp, Error, TEXT("[AssistantApiClient] SendMockResponse EXECUTED. UserText = %s"), *RequestPayload.UserText);

	FAssistantResponse MockResponse;

	const FString LowerText = RequestPayload.UserText.ToLower();

	if (LowerText.Contains(TEXT("regalo")) || LowerText.Contains(TEXT("ragazza")))
	{
		MockResponse.AnswerToSay =
			TEXT("Certo. Per un regalo potresti valutare profumeria, gioielleria, abbigliamento o accessori. ")
			TEXT("Per consigliarti meglio, preferisci qualcosa di beauty, moda, tecnologia o un'esperienza?");

		MockResponse.Confidence = EAssistantConfidence::Medium;
		MockResponse.bNeedsClarification = true;
		MockResponse.ClarificationQuestion =
			TEXT("Preferisci un regalo beauty, moda, tecnologia o un'esperienza?");

		FAssistantShopSuggestion Suggestion;
		Suggestion.Name = TEXT("Esempio Profumeria");
		Suggestion.Category = TEXT("Profumeria");
		Suggestion.Floor = TEXT("Piano terra");
		Suggestion.Area = TEXT("Area centrale");
		Suggestion.Notes = TEXT("Dato mock temporaneo, da sostituire con knowledge base reale.");

		MockResponse.SuggestedShops.Add(Suggestion);
		MockResponse.UsedSources.Add(TEXT("mock_local_response"));
	}
	else if (LowerText.Contains(TEXT("bagno")) || LowerText.Contains(TEXT("toilette")))
	{
		MockResponse.AnswerToSay =
			TEXT("Nel mock temporaneo posso dirti che i bagni si trovano nell'area centrale del piano terra. ")
			TEXT("Questa informazione andrà sostituita con i dati reali del centro commerciale.");

		MockResponse.Confidence = EAssistantConfidence::Medium;
		MockResponse.UsedSources.Add(TEXT("mock_local_response"));
	}
	else if (LowerText.Contains(TEXT("orario")) || LowerText.Contains(TEXT("aperto")) || LowerText.Contains(TEXT("chiude")))
	{
		MockResponse.AnswerToSay =
			TEXT("Per ora sto usando dati mock. In futuro potrò dirti gli orari reali del centro, dei singoli negozi e degli eventi.");

		MockResponse.Confidence = EAssistantConfidence::Low;
		MockResponse.bNeedsClarification = true;
		MockResponse.ClarificationQuestion =
			TEXT("Ti interessano gli orari del centro commerciale o di un negozio specifico?");

		MockResponse.UsedSources.Add(TEXT("mock_local_response"));
	}
	else
	{
		MockResponse.AnswerToSay =
			TEXT("Posso aiutarti con negozi, servizi, eventi, orari e suggerimenti per lo shopping nel centro commerciale. ")
			TEXT("In questa fase sto usando una risposta mock, quindi non ho ancora i dati reali del centro.");

		MockResponse.Confidence = EAssistantConfidence::Low;
		MockResponse.bNeedsClarification = true;
		MockResponse.ClarificationQuestion =
			TEXT("Vuoi cercare un negozio, un servizio, un evento o un'idea regalo?");

		MockResponse.UsedSources.Add(TEXT("mock_local_response"));
	}

	MockResponse.DebugInfo = TEXT("Generated locally because EndpointUrl is empty.");

	OnApiCompleted.Broadcast(true, MockResponse);
}