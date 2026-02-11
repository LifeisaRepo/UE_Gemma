// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LiteRTLMFunctionLib.generated.h"

// Add a dynamic delegate so Blueprints can "bind" to the AI's response
DECLARE_DYNAMIC_DELEGATE_OneParam(FLiteRTResponseDelegate, FString, Response);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLiteRTToolResultResponseDelegate, FString, Response);

/**
 * 
 */
UCLASS()
class LITERTLMPLUGIN_API ULiteRTLMFunctionLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
    // Expose this to Blueprints so you can trigger it from your UI/PlayerController
    UFUNCTION(BlueprintCallable, Category = "LiteRTLM")
    static void InitializeLM(FString ModelPath);

    // This is the function you call from Blueprints
    UFUNCTION(BlueprintCallable, Category = "LiteRTLM")
    static void GenerateLMResponseAsync(FString Prompt, FLiteRTResponseDelegate OnComplete);

    UFUNCTION(BlueprintCallable, Category = "LiteRTLM")
    static void SubmitToolResult(FString FunctionName, FString JsonResults, FLiteRTToolResultResponseDelegate OnComplete);

    UFUNCTION(BlueprintCallable, Category = "LiteRTLM")
    static void ResetConversation();

    UFUNCTION(BlueprintCallable, Category = "LiteRTLM")
    static bool ParseFunctionCall(FString RawResponse, FString& OutFunctionName, TMap<FString, FString>& OutParameters);

    UFUNCTION(BlueprintCallable, Category = "LiteRTLM")
    static void ShutdownLM();

    UFUNCTION(BlueprintCallable, Category = "LiteRT|STT")
    static void InitSTT();

    UFUNCTION(BlueprintCallable, Category = "LiteRT|STT")
    static void StartSTT();

    UFUNCTION(BlueprintCallable, Category = "LiteRT|STT")
    static void StopSTT();

    UFUNCTION(BlueprintCallable, Category = "LiteRT|Lifecycle")
    static void ShutdownAIServices();
};

#if PLATFORM_ANDROID
extern "C" {
    JNIEXPORT void JNICALL Java_com_epicgames_unreal_GameActivity_nativeOnSTTResult(JNIEnv* jenv, jobject thiz, jstring jtext);
}
#endif
